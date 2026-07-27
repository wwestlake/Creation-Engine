#include "module_builder.h"

#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>

namespace ce::lang::jit {

namespace {

// Thrown for anything BuildModule doesn't (yet) support -- caught once,
// at BuildModule's own top level, and turned into the errorOut string.
// Purely an internal control-flow convenience for this translation
// unit; never crosses into JIT'd code or an ABI boundary (the "no
// exceptions across JIT frames" rule is about the *generated* code,
// not the host-side compiler that generates it).
struct CodegenError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// A single loop's break/continue targets, so nested loops each get
// their own pair without threading them through every CodegenStmt call.
struct LoopTargets {
    llvm::BasicBlock* breakTarget;
    llvm::BasicBlock* continueTarget;
};

// Mirrors intrinsics.def's purity column -- drives the LLVM memory-access
// attribute put on a *declared* (never-defined-in-this-module) ABI
// function. Getting ReadsWorld/Mutates wrong as Pure would let the
// optimizer illegally hoist/reorder a call above another that mutates
// the same World -- see intrinsics.def's own comment.
enum class Purity { Pure, ReadsWorld, Mutates };

// One real host-ABI intrinsic's shape, as declared in intrinsics.def.
// Populated once from the X-macro table; empty cSymbol entries are the
// 14 "pure computation" math/vec3 intrinsics GS4 already lowers
// directly to LLVM IR (CodegenBuiltinIntrinsic below) -- CodegenCall
// tries that path FIRST and only consults this table as a fallback, so
// an empty cSymbol reaching CodegenAbiCall would be an internal error.
struct AbiIntrinsicInfo {
    std::string cSymbol;
    Purity purity;
    Type returnType;
    std::vector<Type> paramTypes;
};

std::unordered_map<std::string, AbiIntrinsicInfo> BuildAbiIntrinsicTable() {
    std::unordered_map<std::string, AbiIntrinsicInfo> table;
// `domain` (GS-Interop) is deliberately unused here: codegen never sees
// a blocked-domain call in the first place (sema.cpp's CheckCall rejects
// it before BuildModule ever runs on that program), so this table has
// no need to know about it -- see the GS-Interop plan's own note on why
// module_builder.cpp needs no behavior change, just this positional
// parameter to keep matching intrinsics.def's macro shape.
#define CEL_INTRINSIC0(name, cSymbol, purity, domain, ret) \
    table[#name] = AbiIntrinsicInfo{ #cSymbol, Purity::purity, Type::ret, {} };
#define CEL_INTRINSIC1(name, cSymbol, purity, domain, ret, p1) \
    table[#name] = AbiIntrinsicInfo{ #cSymbol, Purity::purity, Type::ret, { Type::p1 } };
#define CEL_INTRINSIC2(name, cSymbol, purity, domain, ret, p1, p2) \
    table[#name] = AbiIntrinsicInfo{ #cSymbol, Purity::purity, Type::ret, { Type::p1, Type::p2 } };
#define CEL_INTRINSIC3(name, cSymbol, purity, domain, ret, p1, p2, p3) \
    table[#name] = AbiIntrinsicInfo{ #cSymbol, Purity::purity, Type::ret, { Type::p1, Type::p2, Type::p3 } };
#include "lang/intrinsics.def"
#undef CEL_INTRINSIC0
#undef CEL_INTRINSIC1
#undef CEL_INTRINSIC2
#undef CEL_INTRINSIC3
    return table;
}

// Every name CodegenBuiltinIntrinsic lowers directly to pure IR -- used
// to tell "genuinely not an ABI call" apart from "ABI table lookup
// failed", so a real internal error still throws instead of silently
// misdispatching.
bool IsPureIrIntrinsic(const std::string& name) {
    static const std::unordered_map<std::string, bool> pureNames = {
        { "sqrt", true },  { "abs", true },     { "min", true },       { "max", true },  { "floor", true },
        { "sin", true },   { "cos", true },     { "clamp", true },     { "lerp", true }, { "vec3", true },
        { "dot", true },   { "cross", true },   { "length", true },    { "normalize", true },
    };
    return pureNames.count(name) != 0;
}

class CodeGenerator {
public:
    CodeGenerator(llvm::LLVMContext& context, llvm::Module& module)
        : context_(context), module_(module), builder_(context), abiIntrinsics_(BuildAbiIntrinsicTable()) {
        vec3Type_ = llvm::StructType::get(context_, { builder_.getFloatTy(), builder_.getFloatTy(), builder_.getFloatTy() },
                                           /*isPacked=*/false);
        scriptContextPtrTy_ = builder_.getPtrTy();
    }

    void Build(Program& program) {
        DeclareFunctions(program);
        DeclareGlobals(program);
        for (Decl* decl : program.decls) {
            if (decl->kind == DeclKind::Func) {
                CodegenFunctionBody(*decl->funcDecl);
            }
        }

        std::string verifyError;
        llvm::raw_string_ostream verifyStream(verifyError);
        if (llvm::verifyModule(module_, &verifyStream)) {
            throw CodegenError("generated module failed verification: " + verifyStream.str());
        }
    }

private:
    // --- Types -----------------------------------------------------------

    llvm::Type* MapType(Type t) {
        switch (t) {
            case Type::Int: return builder_.getInt64Ty();
            case Type::Float: return builder_.getFloatTy();
            case Type::Bool: return builder_.getInt1Ty();
            case Type::Vec3: return vec3Type_;
            case Type::Entity: return builder_.getInt64Ty();
            case Type::Void: return builder_.getVoidTy();
            case Type::String:
            case Type::Unknown:
                break;
        }
        throw CodegenError("internal error: cannot map type '" + std::string(ToString(t)) + "' to an LLVM type");
    }

    // --- Declarations ------------------------------------------------------

    void DeclareFunctions(Program& program) {
        for (Decl* decl : program.decls) {
            if (decl->kind != DeclKind::Func) {
                continue;
            }
            FuncDecl& f = *decl->funcDecl;
            // Every CEL function's REAL LLVM signature takes an implicit
            // leading ScriptContext* invisible at the CEL source level
            // (sema's FunctionSignature never sees it) -- so a script
            // calling get_position/set_position/... has something to
            // forward, and so `celc --run-world`'s driver can pass one
            // real World in without CEL needing a language-level concept
            // of it at all.
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(scriptContextPtrTy_);
            for (const Param& p : f.params) {
                paramTypes.push_back(MapType(ParseTypeName(p.type)));
            }
            llvm::Type* returnType = MapType(f.returnType.empty() ? Type::Void : ParseTypeName(f.returnType));
            auto* fnType = llvm::FunctionType::get(returnType, paramTypes, /*isVarArg=*/false);
            auto* fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, f.name, module_);
            fn->setDoesNotThrow();
            functions_[f.name] = fn;
        }
    }

    void DeclareGlobals(Program& program) {
        for (Decl* decl : program.decls) {
            if (decl->kind != DeclKind::Var) {
                continue;
            }
            GlobalVarDecl& v = *decl->varDecl;
            llvm::Constant* init = ConstantFold(*v.initExpr);
            if (init == nullptr) {
                throw CodegenError("global '" + v.name +
                                    "' must be initialized with a literal constant (module-level globals with a "
                                    "computed initializer are deferred to a later milestone)");
            }
            auto* gv = new llvm::GlobalVariable(module_, MapType(v.resolvedType), /*isConstant=*/false,
                                                 llvm::GlobalValue::InternalLinkage, init, v.name);
            globals_[v.name] = gv;
        }
    }

    // Folds a literal-only expr (after sema's own const-folding, only
    // compound literal arithmetic should ever reach here as a single
    // literal node already, but this stays recursive in case a future
    // milestone loosens that) into an llvm::Constant, or returns nullptr
    // if it isn't one -- global initializers must be constants; DeclareGlobals
    // reports a clear error rather than attempting general runtime
    // initialization, which is out of this milestone's scope.
    llvm::Constant* ConstantFold(const Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLiteral: return llvm::ConstantInt::get(builder_.getInt64Ty(), static_cast<uint64_t>(e.intValue), true);
            case ExprKind::FloatLiteral: return llvm::ConstantFP::get(builder_.getFloatTy(), e.floatValue);
            case ExprKind::BoolLiteral: return llvm::ConstantInt::get(builder_.getInt1Ty(), e.boolValue ? 1 : 0);
            default: return nullptr;
        }
    }

    // --- Scopes (mirrors sema's own scope stack, one alloca per local) ----

    void PushScope() { locals_.emplace_back(); }
    void PopScope() { locals_.pop_back(); }

    llvm::AllocaInst* LookupLocal(const std::string& name) const {
        for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return nullptr;
    }

    // Entry-block allocas: always inserted at the start of the current
    // function's entry block regardless of where the builder's main
    // insertion point currently is, so mem2reg/SROA can promote every
    // local to an SSA register during optimization -- the pattern the
    // GS scripting plan calls for explicitly.
    llvm::AllocaInst* CreateEntryBlockAlloca(llvm::Function* fn, const std::string& name, llvm::Type* type) {
        llvm::IRBuilder<> entryBuilder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
        return entryBuilder.CreateAlloca(type, nullptr, name);
    }

    // --- Functions ----------------------------------------------------

    void CodegenFunctionBody(FuncDecl& f) {
        llvm::Function* fn = functions_.at(f.name);
        currentFunction_ = fn;
        loopStack_.clear();

        auto* entryBB = llvm::BasicBlock::Create(context_, "entry", fn);
        builder_.SetInsertPoint(entryBB);

        PushScope();
        auto argIt = fn->arg_begin();
        // Argument 0 is always the implicit ScriptContext* DeclareFunctions
        // prepended -- captured once per function and forwarded to every
        // call this body makes (user function or intrinsic), never
        // exposed as a named CEL local.
        llvm::Argument* ctxArg = &*argIt++;
        ctxArg->setName("__ctx");
        currentScriptContextArg_ = ctxArg;
        for (const Param& p : f.params) {
            llvm::Argument* arg = &*argIt++;
            arg->setName(p.name);
            llvm::AllocaInst* alloca = CreateEntryBlockAlloca(fn, p.name, arg->getType());
            builder_.CreateStore(arg, alloca);
            locals_.back()[p.name] = alloca;
        }

        CodegenStmt(*f.body);

        // sema's return-on-all-paths check guarantees a non-void function
        // always hits an explicit `return` before falling off the end of
        // its body -- so if the block the builder ends up in here still
        // has no terminator, it can only be one of StartDeadBlock's
        // never-branched-to blocks (created right after that explicit
        // return/break/continue so the builder has *somewhere* valid to
        // keep inserting into for any dead code textually after it).
        // LLVM's verifier checks every block's terminator against the
        // function's return type regardless of reachability, so a
        // genuinely void function gets `ret void` here, but a non-void
        // one needs `unreachable` -- there's no real value to return
        // from code that can never execute, and `ret void` there would
        // be a real type mismatch, not a formality.
        if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
            if (fn->getReturnType()->isVoidTy()) {
                builder_.CreateRetVoid();
            } else {
                builder_.CreateUnreachable();
            }
        }
        PopScope();
    }

    // --- Statements ------------------------------------------------------

    void CodegenStmt(Stmt& s) {
        switch (s.kind) {
            case StmtKind::Block: {
                PushScope();
                for (Stmt* child : s.statements) {
                    CodegenStmt(*child);
                }
                PopScope();
                return;
            }
            case StmtKind::VarDecl: {
                llvm::Value* initVal = CodegenExpr(*s.initExpr);
                llvm::AllocaInst* alloca = CreateEntryBlockAlloca(currentFunction_, s.name, MapType(s.resolvedType));
                builder_.CreateStore(initVal, alloca);
                locals_.back()[s.name] = alloca;
                return;
            }
            case StmtKind::Assign: {
                llvm::Value* value = CodegenExpr(*s.assignValue);
                if (s.assignOp != AssignOp::Assign) {
                    llvm::Value* current = CodegenExpr(*s.assignTarget);
                    value = ApplyCompoundOp(s.assignOp, current, value, s.assignTarget->type);
                }
                StoreToLvalue(*s.assignTarget, value);
                return;
            }
            case StmtKind::If: {
                llvm::Value* cond = CodegenExpr(*s.condition);
                auto* thenBB = llvm::BasicBlock::Create(context_, "then", currentFunction_);
                auto* elseBB = llvm::BasicBlock::Create(context_, "else", currentFunction_);
                auto* mergeBB = llvm::BasicBlock::Create(context_, "ifcont", currentFunction_);
                builder_.CreateCondBr(cond, thenBB, elseBB);

                builder_.SetInsertPoint(thenBB);
                CodegenStmt(*s.thenBranch);
                if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(mergeBB);
                }

                builder_.SetInsertPoint(elseBB);
                if (s.elseBranch != nullptr) {
                    CodegenStmt(*s.elseBranch);
                }
                if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(mergeBB);
                }

                builder_.SetInsertPoint(mergeBB);
                return;
            }
            case StmtKind::While: {
                auto* condBB = llvm::BasicBlock::Create(context_, "whilecond", currentFunction_);
                auto* bodyBB = llvm::BasicBlock::Create(context_, "whilebody", currentFunction_);
                auto* afterBB = llvm::BasicBlock::Create(context_, "whileafter", currentFunction_);
                builder_.CreateBr(condBB);

                builder_.SetInsertPoint(condBB);
                llvm::Value* cond = CodegenExpr(*s.condition);
                builder_.CreateCondBr(cond, bodyBB, afterBB);

                builder_.SetInsertPoint(bodyBB);
                EmitWatchdogCheck();
                loopStack_.push_back({ afterBB, condBB });
                CodegenStmt(*s.body);
                loopStack_.pop_back();
                if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(condBB);
                }

                builder_.SetInsertPoint(afterBB);
                return;
            }
            case StmtKind::For: {
                PushScope();
                if (s.forInit != nullptr) {
                    CodegenStmt(*s.forInit);
                }
                auto* condBB = llvm::BasicBlock::Create(context_, "forcond", currentFunction_);
                auto* bodyBB = llvm::BasicBlock::Create(context_, "forbody", currentFunction_);
                auto* stepBB = llvm::BasicBlock::Create(context_, "forstep", currentFunction_);
                auto* afterBB = llvm::BasicBlock::Create(context_, "forafter", currentFunction_);
                builder_.CreateBr(condBB);

                builder_.SetInsertPoint(condBB);
                llvm::Value* cond = CodegenExpr(*s.forCond);
                builder_.CreateCondBr(cond, bodyBB, afterBB);

                builder_.SetInsertPoint(bodyBB);
                EmitWatchdogCheck();
                // continue jumps to the step block, not straight back to
                // the condition, so `continue` still runs the step --
                // exactly like C's for-loop semantics.
                loopStack_.push_back({ afterBB, stepBB });
                CodegenStmt(*s.body);
                loopStack_.pop_back();
                if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(stepBB);
                }

                builder_.SetInsertPoint(stepBB);
                if (s.forStep != nullptr) {
                    CodegenStmt(*s.forStep);
                }
                builder_.CreateBr(condBB);

                builder_.SetInsertPoint(afterBB);
                PopScope();
                return;
            }
            case StmtKind::Break:
                builder_.CreateBr(loopStack_.back().breakTarget);
                StartDeadBlock("afterbreak");
                return;
            case StmtKind::Continue:
                builder_.CreateBr(loopStack_.back().continueTarget);
                StartDeadBlock("aftercontinue");
                return;
            case StmtKind::Return:
                if (s.returnValue != nullptr) {
                    builder_.CreateRet(CodegenExpr(*s.returnValue));
                } else {
                    builder_.CreateRetVoid();
                }
                StartDeadBlock("afterreturn");
                return;
            case StmtKind::ExprStmt:
                CodegenExpr(*s.expr);
                return;
        }
    }

    // The runaway-script watchdog: one call to the real
    // ce_watchdog_tick(ScriptContext*) trampoline at the top of every
    // loop iteration (both While and For enter here right after
    // SetInsertPoint(bodyBB), i.e. every loop back-edge), which
    // decrements ctx->loopBudget and reports whether to keep going.
    // Exhausting the budget (or a context already faulted by something
    // else) makes the CURRENT function return immediately -- a simple,
    // non-exception-based unwind; there is no meaningful value to
    // fabricate for a non-void return here, so a zero-initialized one is
    // used purely to satisfy the verifier; the caller is expected to
    // check ctx->faulted afterward, not trust the returned value.
    void EmitWatchdogCheck() {
        llvm::Function* tickFn = GetOrDeclareAbiFunction("ce_watchdog_tick", builder_.getInt32Ty(),
                                                          { scriptContextPtrTy_ }, Purity::Mutates);
        llvm::Value* keepGoing = builder_.CreateCall(tickFn, { currentScriptContextArg_ });
        llvm::Value* cond = builder_.CreateICmpNE(keepGoing, llvm::ConstantInt::get(builder_.getInt32Ty(), 0));

        auto* okBB = llvm::BasicBlock::Create(context_, "wdok", currentFunction_);
        auto* abortBB = llvm::BasicBlock::Create(context_, "wdabort", currentFunction_);
        builder_.CreateCondBr(cond, okBB, abortBB);

        builder_.SetInsertPoint(abortBB);
        if (currentFunction_->getReturnType()->isVoidTy()) {
            builder_.CreateRetVoid();
        } else {
            builder_.CreateRet(llvm::Constant::getNullValue(currentFunction_->getReturnType()));
        }

        builder_.SetInsertPoint(okBB);
    }

    // After an unconditional branch (break/continue/return), the
    // IRBuilder still needs *some* block to keep inserting into if the
    // AST has more statements textually after it (sema doesn't forbid
    // that, it's just dead code) -- point it at a fresh, unreferenced
    // block rather than trying to detect and skip trailing dead
    // statements ourselves. LLVM's own optimizer deletes it later.
    void StartDeadBlock(const char* name) {
        builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, name, currentFunction_));
    }

    void StoreToLvalue(Expr& target, llvm::Value* value) {
        if (target.kind == ExprKind::Identifier) {
            builder_.CreateStore(value, ResolveVariablePointer(target.text));
            return;
        }
        // Member assignment (`p.x = ...`): rebuild the vec3 with one
        // component replaced, then store the whole struct back --
        // vec3 is a value type here (an LLVM struct value, not a
        // pointer), so "assigning to a field" means "load, insertvalue,
        // store" rather than a pointer-to-field GEP.
        llvm::Value* basePtr = ResolveVariablePointer(target.lhs->text);
        llvm::Value* current = builder_.CreateLoad(vec3Type_, basePtr);
        const unsigned index = FieldIndex(target.text);
        llvm::Value* updated = builder_.CreateInsertValue(current, value, index);
        builder_.CreateStore(updated, basePtr);
    }

    static unsigned FieldIndex(const std::string& field) {
        if (field == "x") return 0;
        if (field == "y") return 1;
        return 2; // sema already validated this is x/y/z; "z" is the only remaining case.
    }

    llvm::Value* ResolveVariablePointer(const std::string& name) {
        if (llvm::AllocaInst* local = LookupLocal(name)) {
            return local;
        }
        const auto globalIt = globals_.find(name);
        if (globalIt != globals_.end()) {
            return globalIt->second;
        }
        throw CodegenError("internal error: unresolved variable '" + name + "' reached codegen");
    }

    llvm::Value* ApplyCompoundOp(AssignOp op, llvm::Value* current, llvm::Value* rhs, Type targetType) {
        switch (op) {
            case AssignOp::AddAssign: return ArithAdd(current, rhs, targetType);
            case AssignOp::SubAssign: return ArithSub(current, rhs, targetType);
            case AssignOp::MulAssign: return ArithMul(current, rhs, targetType);
            case AssignOp::DivAssign: return ArithDiv(current, rhs, targetType);
            case AssignOp::Assign: break;
        }
        throw CodegenError("internal error: unreachable compound assign operator");
    }

    // --- Expressions -----------------------------------------------------

    llvm::Value* CodegenExpr(Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLiteral:
                return llvm::ConstantInt::get(builder_.getInt64Ty(), static_cast<uint64_t>(e.intValue), true);
            case ExprKind::FloatLiteral:
                return llvm::ConstantFP::get(builder_.getFloatTy(), e.floatValue);
            case ExprKind::BoolLiteral:
                return llvm::ConstantInt::get(builder_.getInt1Ty(), e.boolValue ? 1 : 0);
            case ExprKind::StringLiteral:
                throw CodegenError(
                    "internal error: string literals are only valid as a direct intrinsic call argument, "
                    "marshaled by CodegenAbiCall -- one reached general expression codegen, which means sema "
                    "let a string value flow somewhere it shouldn't have");
            case ExprKind::Identifier: {
                llvm::Value* ptr = ResolveVariablePointer(e.text);
                return builder_.CreateLoad(MapType(e.type), ptr, e.text);
            }
            case ExprKind::Binary:
                return CodegenBinary(e);
            case ExprKind::Unary:
                return CodegenUnary(e);
            case ExprKind::Call:
                return CodegenCall(e);
            case ExprKind::Member: {
                llvm::Value* base = CodegenExpr(*e.lhs);
                return builder_.CreateExtractValue(base, FieldIndex(e.text));
            }
        }
        throw CodegenError("internal error: unhandled expression kind reached codegen");
    }

    llvm::Value* Vec3ComponentWise(llvm::Value* a, llvm::Value* b,
                                    const std::function<llvm::Value*(llvm::Value*, llvm::Value*)>& op) {
        llvm::Value* result = llvm::UndefValue::get(vec3Type_);
        for (unsigned i = 0; i < 3; ++i) {
            llvm::Value* ai = builder_.CreateExtractValue(a, i);
            llvm::Value* bi = builder_.CreateExtractValue(b, i);
            result = builder_.CreateInsertValue(result, op(ai, bi), i);
        }
        return result;
    }

    llvm::Value* Vec3ScalarOp(llvm::Value* v, llvm::Value* scalar,
                               const std::function<llvm::Value*(llvm::Value*, llvm::Value*)>& op) {
        llvm::Value* result = llvm::UndefValue::get(vec3Type_);
        for (unsigned i = 0; i < 3; ++i) {
            llvm::Value* vi = builder_.CreateExtractValue(v, i);
            result = builder_.CreateInsertValue(result, op(vi, scalar), i);
        }
        return result;
    }

    llvm::Value* ArithAdd(llvm::Value* lhs, llvm::Value* rhs, Type t) {
        if (t == Type::Int) return builder_.CreateAdd(lhs, rhs, "addtmp");
        if (t == Type::Float) return builder_.CreateFAdd(lhs, rhs, "faddtmp");
        return Vec3ComponentWise(lhs, rhs, [&](llvm::Value* a, llvm::Value* b) { return builder_.CreateFAdd(a, b); });
    }
    llvm::Value* ArithSub(llvm::Value* lhs, llvm::Value* rhs, Type t) {
        if (t == Type::Int) return builder_.CreateSub(lhs, rhs, "subtmp");
        if (t == Type::Float) return builder_.CreateFSub(lhs, rhs, "fsubtmp");
        return Vec3ComponentWise(lhs, rhs, [&](llvm::Value* a, llvm::Value* b) { return builder_.CreateFSub(a, b); });
    }
    llvm::Value* ArithMul(llvm::Value* lhs, llvm::Value* rhs, Type t) {
        if (t == Type::Int) return builder_.CreateMul(lhs, rhs, "multmp");
        if (t == Type::Float) return builder_.CreateFMul(lhs, rhs, "fmultmp");
        // vec3 *= float is the only compound-mul combination sema allows.
        return Vec3ScalarOp(lhs, rhs, [&](llvm::Value* a, llvm::Value* b) { return builder_.CreateFMul(a, b); });
    }
    llvm::Value* ArithDiv(llvm::Value* lhs, llvm::Value* rhs, Type t) {
        if (t == Type::Int) return builder_.CreateSDiv(lhs, rhs, "divtmp");
        if (t == Type::Float) return builder_.CreateFDiv(lhs, rhs, "fdivtmp");
        return Vec3ScalarOp(lhs, rhs, [&](llvm::Value* a, llvm::Value* b) { return builder_.CreateFDiv(a, b); });
    }

    llvm::Value* CodegenBinary(Expr& e) {
        // Binary::lhs/rhs types were resolved by sema and are identical
        // to what drove ITS rule selection -- re-derive the operator's
        // *operand* type from e.lhs->type rather than e.type, since
        // comparisons (e.type == Bool) would otherwise lose which
        // arithmetic type they compared.
        const Type operandType = e.lhs->type;
        llvm::Value* lhs = CodegenExpr(*e.lhs);
        llvm::Value* rhs = CodegenExpr(*e.rhs);

        switch (e.binaryOp) {
            case BinaryOp::Add: return ArithAdd(lhs, rhs, operandType);
            case BinaryOp::Sub: return ArithSub(lhs, rhs, operandType);
            case BinaryOp::Mul: return ArithMul(lhs, rhs, operandType);
            case BinaryOp::Div: return ArithDiv(lhs, rhs, operandType);
            case BinaryOp::Mod: return builder_.CreateSRem(lhs, rhs, "modtmp"); // int-only per sema.
            case BinaryOp::Eq: return CodegenCompare(lhs, rhs, operandType, /*negate=*/false);
            case BinaryOp::Neq: return CodegenCompare(lhs, rhs, operandType, /*negate=*/true);
            case BinaryOp::Lt:
                return operandType == Type::Int ? builder_.CreateICmpSLT(lhs, rhs) : builder_.CreateFCmpOLT(lhs, rhs);
            case BinaryOp::Gt:
                return operandType == Type::Int ? builder_.CreateICmpSGT(lhs, rhs) : builder_.CreateFCmpOGT(lhs, rhs);
            case BinaryOp::Le:
                return operandType == Type::Int ? builder_.CreateICmpSLE(lhs, rhs) : builder_.CreateFCmpOLE(lhs, rhs);
            case BinaryOp::Ge:
                return operandType == Type::Int ? builder_.CreateICmpSGE(lhs, rhs) : builder_.CreateFCmpOGE(lhs, rhs);
            case BinaryOp::And: return builder_.CreateAnd(lhs, rhs, "andtmp");
            case BinaryOp::Or: return builder_.CreateOr(lhs, rhs, "ortmp");
        }
        throw CodegenError("internal error: unhandled binary operator reached codegen");
    }

    llvm::Value* CodegenCompare(llvm::Value* lhs, llvm::Value* rhs, Type t, bool negate) {
        llvm::Value* eq = nullptr;
        switch (t) {
            case Type::Int:
            case Type::Bool:
            case Type::Entity:
                eq = builder_.CreateICmpEQ(lhs, rhs);
                break;
            case Type::Float:
                eq = builder_.CreateFCmpOEQ(lhs, rhs);
                break;
            case Type::Vec3: {
                llvm::Value* eqX = builder_.CreateFCmpOEQ(builder_.CreateExtractValue(lhs, 0), builder_.CreateExtractValue(rhs, 0));
                llvm::Value* eqY = builder_.CreateFCmpOEQ(builder_.CreateExtractValue(lhs, 1), builder_.CreateExtractValue(rhs, 1));
                llvm::Value* eqZ = builder_.CreateFCmpOEQ(builder_.CreateExtractValue(lhs, 2), builder_.CreateExtractValue(rhs, 2));
                eq = builder_.CreateAnd(builder_.CreateAnd(eqX, eqY), eqZ);
                break;
            }
            default:
                throw CodegenError("internal error: equality on an unsupported type reached codegen");
        }
        return negate ? builder_.CreateNot(eq) : eq;
    }

    llvm::Value* CodegenUnary(Expr& e) {
        llvm::Value* operand = CodegenExpr(*e.lhs);
        if (e.unaryOp == UnaryOp::Not) {
            return builder_.CreateNot(operand, "nottmp");
        }
        // Neg
        if (e.lhs->type == Type::Int) return builder_.CreateNeg(operand, "negtmp");
        if (e.lhs->type == Type::Float) return builder_.CreateFNeg(operand, "fnegtmp");
        return Vec3ComponentWise(operand, operand, [&](llvm::Value* a, llvm::Value*) { return builder_.CreateFNeg(a); });
    }

    llvm::Function* GetLLVMIntrinsic(llvm::Intrinsic::ID id, llvm::Type* type) {
        return llvm::Intrinsic::getDeclaration(&module_, id, { type });
    }

    // The v1 "pure computation" math/vec3 intrinsics -- lowered directly
    // to LLVM IR (LLVM's own standard intrinsics where one exists, or
    // composed arithmetic otherwise), with no external call and no ABI
    // involved at all. Everything else in intrinsics.def (world/entity/
    // transform/debug, plus atan2) is a genuine engine intrinsic, handled
    // by CodegenAbiCall instead -- returns nullptr for those so
    // CodegenCall knows to fall through.
    llvm::Value* CodegenBuiltinIntrinsic(const std::string& name, std::vector<llvm::Value*>& args) {
        auto callUnaryFloatIntrinsic = [&](llvm::Intrinsic::ID id) {
            return builder_.CreateCall(GetLLVMIntrinsic(id, builder_.getFloatTy()), args[0]);
        };
        auto callBinaryFloatIntrinsic = [&](llvm::Intrinsic::ID id) {
            return builder_.CreateCall(GetLLVMIntrinsic(id, builder_.getFloatTy()), { args[0], args[1] });
        };

        if (name == "sqrt") return callUnaryFloatIntrinsic(llvm::Intrinsic::sqrt);
        if (name == "abs") return callUnaryFloatIntrinsic(llvm::Intrinsic::fabs);
        if (name == "floor") return callUnaryFloatIntrinsic(llvm::Intrinsic::floor);
        if (name == "sin") return callUnaryFloatIntrinsic(llvm::Intrinsic::sin);
        if (name == "cos") return callUnaryFloatIntrinsic(llvm::Intrinsic::cos);
        if (name == "min") return callBinaryFloatIntrinsic(llvm::Intrinsic::minnum);
        if (name == "max") return callBinaryFloatIntrinsic(llvm::Intrinsic::maxnum);
        if (name == "clamp") {
            llvm::Value* maxed = builder_.CreateCall(GetLLVMIntrinsic(llvm::Intrinsic::maxnum, builder_.getFloatTy()),
                                                      { args[0], args[1] });
            return builder_.CreateCall(GetLLVMIntrinsic(llvm::Intrinsic::minnum, builder_.getFloatTy()), { maxed, args[2] });
        }
        if (name == "lerp") {
            // a + (b - a) * t
            llvm::Value* diff = builder_.CreateFSub(args[1], args[0]);
            llvm::Value* scaled = builder_.CreateFMul(diff, args[2]);
            return builder_.CreateFAdd(args[0], scaled);
        }
        if (name == "vec3") {
            llvm::Value* result = llvm::UndefValue::get(vec3Type_);
            result = builder_.CreateInsertValue(result, args[0], 0);
            result = builder_.CreateInsertValue(result, args[1], 1);
            result = builder_.CreateInsertValue(result, args[2], 2);
            return result;
        }
        if (name == "dot") {
            return Dot(args[0], args[1]);
        }
        if (name == "cross") {
            // (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)
            llvm::Value* ax = builder_.CreateExtractValue(args[0], 0);
            llvm::Value* ay = builder_.CreateExtractValue(args[0], 1);
            llvm::Value* az = builder_.CreateExtractValue(args[0], 2);
            llvm::Value* bx = builder_.CreateExtractValue(args[1], 0);
            llvm::Value* by = builder_.CreateExtractValue(args[1], 1);
            llvm::Value* bz = builder_.CreateExtractValue(args[1], 2);
            llvm::Value* rx = builder_.CreateFSub(builder_.CreateFMul(ay, bz), builder_.CreateFMul(az, by));
            llvm::Value* ry = builder_.CreateFSub(builder_.CreateFMul(az, bx), builder_.CreateFMul(ax, bz));
            llvm::Value* rz = builder_.CreateFSub(builder_.CreateFMul(ax, by), builder_.CreateFMul(ay, bx));
            llvm::Value* result = llvm::UndefValue::get(vec3Type_);
            result = builder_.CreateInsertValue(result, rx, 0);
            result = builder_.CreateInsertValue(result, ry, 1);
            result = builder_.CreateInsertValue(result, rz, 2);
            return result;
        }
        if (name == "length") {
            return builder_.CreateCall(GetLLVMIntrinsic(llvm::Intrinsic::sqrt, builder_.getFloatTy()), Dot(args[0], args[0]));
        }
        if (name == "normalize") {
            llvm::Value* len =
                builder_.CreateCall(GetLLVMIntrinsic(llvm::Intrinsic::sqrt, builder_.getFloatTy()), Dot(args[0], args[0]));
            return Vec3ScalarOp(args[0], len, [&](llvm::Value* a, llvm::Value* b) { return builder_.CreateFDiv(a, b); });
        }
        return nullptr;
    }

    llvm::Value* Dot(llvm::Value* a, llvm::Value* b) {
        llvm::Value* ax = builder_.CreateExtractValue(a, 0);
        llvm::Value* ay = builder_.CreateExtractValue(a, 1);
        llvm::Value* az = builder_.CreateExtractValue(a, 2);
        llvm::Value* bx = builder_.CreateExtractValue(b, 0);
        llvm::Value* by = builder_.CreateExtractValue(b, 1);
        llvm::Value* bz = builder_.CreateExtractValue(b, 2);
        llvm::Value* sum = builder_.CreateFMul(ax, bx);
        sum = builder_.CreateFAdd(sum, builder_.CreateFMul(ay, by));
        sum = builder_.CreateFAdd(sum, builder_.CreateFMul(az, bz));
        return sum;
    }

    // Declares (once, cached by symbol) an extern function this module
    // never defines -- resolved at JIT time by the host process's
    // absoluteSymbols registration (Runtime, via GetAbiTrampolines()).
    // The purity column drives the one LLVM attribute that matters here:
    // ReadsWorld gets `readonly` (may read anything, including through
    // ctx, but never writes), Pure gets `readnone` (touches no memory at
    // all -- only genuinely true for atan2 and the watchdog... no,
    // watchdog mutates ctx's budget, so it's Mutates), Mutates gets no
    // memory-access attribute, since it may freely read and write.
    llvm::Function* GetOrDeclareAbiFunction(const std::string& cSymbol, llvm::Type* returnType,
                                             const std::vector<llvm::Type*>& paramTypes, Purity purity) {
        const auto cached = abiFunctionDecls_.find(cSymbol);
        if (cached != abiFunctionDecls_.end()) {
            return cached->second;
        }
        auto* fnType = llvm::FunctionType::get(returnType, paramTypes, /*isVarArg=*/false);
        auto* fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, cSymbol, module_);
        fn->setDoesNotThrow();
        switch (purity) {
            case Purity::Pure: fn->setDoesNotAccessMemory(); break;
            case Purity::ReadsWorld: fn->setOnlyReadsMemory(); break;
            case Purity::Mutates: break;
        }
        abiFunctionDecls_[cSymbol] = fn;
        return fn;
    }

    // Marshals a genuine host-ABI intrinsic call: ScriptContext* always
    // leads; Vec3 arguments/return cross as float* (an alloca'd
    // temporary for an argument, a trailing out-param for a return
    // value, per the ABI's rule 1); String arguments (always a direct
    // literal -- sema enforces this, see NonLiteralStringArgument) cross
    // as a (const char*, int64_t length) pair rather than going through
    // CodegenExpr at all; Bool crosses as i32, per rule 2, widened/
    // truncated at the boundary.
    llvm::Value* CodegenAbiCall(const std::string& name, const AbiIntrinsicInfo& info, std::vector<Expr*>& argExprs) {
        std::vector<llvm::Type*> declParamTypes;
        std::vector<llvm::Value*> callArgs;
        declParamTypes.push_back(scriptContextPtrTy_);
        callArgs.push_back(currentScriptContextArg_);

        for (std::size_t i = 0; i < argExprs.size(); ++i) {
            const Type paramType = info.paramTypes[i];
            if (paramType == Type::String) {
                Expr& argExpr = *argExprs[i];
                if (argExpr.kind != ExprKind::StringLiteral) {
                    throw CodegenError("internal error: intrinsic '" + name +
                                        "' expects a string LITERAL argument (sema should have enforced this)");
                }
                llvm::Constant* strPtr = builder_.CreateGlobalStringPtr(argExpr.text);
                declParamTypes.push_back(builder_.getPtrTy());
                declParamTypes.push_back(builder_.getInt64Ty());
                callArgs.push_back(strPtr);
                callArgs.push_back(llvm::ConstantInt::get(builder_.getInt64Ty(), argExpr.text.size()));
                continue;
            }

            llvm::Value* value = CodegenExpr(*argExprs[i]);
            if (paramType == Type::Vec3) {
                llvm::AllocaInst* slot = CreateEntryBlockAlloca(currentFunction_, "vec3arg", vec3Type_);
                builder_.CreateStore(value, slot);
                declParamTypes.push_back(builder_.getPtrTy());
                callArgs.push_back(slot);
            } else if (paramType == Type::Bool) {
                declParamTypes.push_back(builder_.getInt32Ty());
                callArgs.push_back(builder_.CreateZExt(value, builder_.getInt32Ty()));
            } else {
                declParamTypes.push_back(MapType(paramType));
                callArgs.push_back(value);
            }
        }

        const bool vec3Return = info.returnType == Type::Vec3;
        llvm::AllocaInst* vec3RetSlot = nullptr;
        llvm::Type* declaredReturnType;
        if (vec3Return) {
            vec3RetSlot = CreateEntryBlockAlloca(currentFunction_, "vec3ret", vec3Type_);
            declParamTypes.push_back(builder_.getPtrTy());
            callArgs.push_back(vec3RetSlot);
            declaredReturnType = builder_.getVoidTy();
        } else if (info.returnType == Type::Bool) {
            declaredReturnType = builder_.getInt32Ty();
        } else {
            declaredReturnType = MapType(info.returnType);
        }

        // A Vec3-returning intrinsic writes through its trailing out-param
        // pointer even when intrinsics.def calls it ReadsWorld/Pure (that
        // column describes whether it touches the *World*, not whether it
        // writes its own return slot) -- declaring it `readonly`/`readnone`
        // anyway would be a lie the optimizer takes literally: at -O2 it
        // legally treats the store the call was supposed to make as never
        // having happened and elides it, leaving the destination alloca
        // uninitialized (this was caught for real: gs5_world_spawn_grid and
        // gs5_world_orbit_steps both printed "nan" against get_position
        // before this fix). Force Mutates for the declaration whenever a
        // vec3 out-param is present, regardless of the table's purity.
        const Purity declaredPurity = vec3Return ? Purity::Mutates : info.purity;
        llvm::Function* callee = GetOrDeclareAbiFunction(info.cSymbol, declaredReturnType, declParamTypes, declaredPurity);
        llvm::Value* call = builder_.CreateCall(callee, callArgs);

        if (vec3Return) {
            return builder_.CreateLoad(vec3Type_, vec3RetSlot);
        }
        if (info.returnType == Type::Bool) {
            return builder_.CreateTrunc(call, builder_.getInt1Ty());
        }
        return call;
    }

    llvm::Value* CodegenCall(Expr& e) {
        const std::string& name = e.lhs->text;

        const auto userFnIt = functions_.find(name);
        if (userFnIt != functions_.end()) {
            std::vector<llvm::Value*> args;
            args.push_back(currentScriptContextArg_);
            for (Expr* arg : e.args) {
                args.push_back(CodegenExpr(*arg));
            }
            return builder_.CreateCall(userFnIt->second, args);
        }

        if (IsPureIrIntrinsic(name)) {
            std::vector<llvm::Value*> args;
            for (Expr* arg : e.args) {
                args.push_back(CodegenExpr(*arg));
            }
            llvm::Value* result = CodegenBuiltinIntrinsic(name, args);
            if (result == nullptr) {
                throw CodegenError("internal error: '" + name + "' is registered as a pure-IR intrinsic but "
                                    "CodegenBuiltinIntrinsic didn't handle it");
            }
            return result;
        }

        const auto abiIt = abiIntrinsics_.find(name);
        if (abiIt != abiIntrinsics_.end()) {
            return CodegenAbiCall(name, abiIt->second, e.args);
        }

        throw CodegenError("internal error: unresolved call to '" + name + "' reached codegen (sema should have "
                            "rejected an unknown function name)");
    }

    llvm::LLVMContext& context_;
    llvm::Module& module_;
    llvm::IRBuilder<> builder_;
    llvm::StructType* vec3Type_;
    llvm::Type* scriptContextPtrTy_;

    std::unordered_map<std::string, llvm::Function*> functions_;
    std::unordered_map<std::string, llvm::GlobalVariable*> globals_;
    std::unordered_map<std::string, AbiIntrinsicInfo> abiIntrinsics_;
    std::unordered_map<std::string, llvm::Function*> abiFunctionDecls_;
    std::vector<std::unordered_map<std::string, llvm::AllocaInst*>> locals_;
    std::vector<LoopTargets> loopStack_;
    llvm::Function* currentFunction_ = nullptr;
    llvm::Value* currentScriptContextArg_ = nullptr;
};

} // namespace

std::unique_ptr<llvm::Module> BuildModule(llvm::LLVMContext& context, const std::string& moduleName, Program& program,
                                           std::string& errorOut) {
    auto module = std::make_unique<llvm::Module>(moduleName, context);
    try {
        CodeGenerator generator(context, *module);
        generator.Build(program);
    } catch (const CodegenError& err) {
        errorOut = err.what();
        return nullptr;
    }
    return module;
}

} // namespace ce::lang::jit
