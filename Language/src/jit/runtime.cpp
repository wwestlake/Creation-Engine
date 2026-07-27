#include "lang/jit/runtime.h"

#include <iostream>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>

#include "module_builder.h"
#include "optimizer.h"

// This translation unit (and module_builder.cpp/optimizer.cpp alongside
// it) are the only places in the whole project an llvm/*.h header is
// allowed to appear -- see runtime.h's own comment.

namespace ce::lang::jit {

namespace {

// The "host function" a JIT'd script would call back into. extern "C"
// (unmangled name) and noexcept -- both are permanent ABI rules every
// real intrinsic (GS5) will follow: JIT'd Win64 frames carry no unwind
// info for a C++ exception to cross, so host trampolines must never
// throw.
extern "C" int64_t ce_selftest_host_add(int64_t a, int64_t b) noexcept {
    return a + b;
}

} // namespace

struct Runtime::Impl {
    std::unique_ptr<llvm::orc::LLJIT> lljit;
};

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {
    // One-time process-wide LLVM native-target registration. Safe to
    // call more than once across multiple Runtime instances -- LLVM's
    // Initialize* functions are idempotent -- but only ever needs to
    // happen once, so guard it anyway to keep the intent obvious.
    static const bool initialized = [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        return true;
    }();
    (void)initialized;
}

Runtime::~Runtime() = default;

bool Runtime::RunSelfTest() {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("gs1_selftest", *context);

    llvm::IRBuilder<> builder(*context);
    llvm::Type* i64 = builder.getInt64Ty();

    // declare i64 @ce_selftest_host_add(i64, i64)
    auto* hostFnType = llvm::FunctionType::get(i64, { i64, i64 }, false);
    auto* hostFn = llvm::Function::Create(hostFnType, llvm::Function::ExternalLinkage, "ce_selftest_host_add",
                                           module.get());
    hostFn->setDoesNotThrow(); // nounwind -- matches the ABI rule every real intrinsic call will carry.

    // define i64 @add(i64 %a, i64 %b) { %sum = add i64 %a, %b; ret i64 %sum }
    auto* addFnType = llvm::FunctionType::get(i64, { i64, i64 }, false);
    auto* addFn = llvm::Function::Create(addFnType, llvm::Function::ExternalLinkage, "add", module.get());
    {
        auto* entry = llvm::BasicBlock::Create(*context, "entry", addFn);
        builder.SetInsertPoint(entry);
        auto argIt = addFn->args().begin();
        llvm::Value* a = &*argIt++;
        llvm::Value* b = &*argIt;
        builder.CreateRet(builder.CreateAdd(a, b, "sum"));
    }

    // define i64 @call_host(i64 %a, i64 %b) { %r = call i64 @ce_selftest_host_add(i64 %a, i64 %b); ret i64 %r }
    auto* callHostFnType = llvm::FunctionType::get(i64, { i64, i64 }, false);
    auto* callHostFn =
        llvm::Function::Create(callHostFnType, llvm::Function::ExternalLinkage, "call_host", module.get());
    {
        auto* entry = llvm::BasicBlock::Create(*context, "entry", callHostFn);
        builder.SetInsertPoint(entry);
        auto argIt = callHostFn->args().begin();
        llvm::Value* a = &*argIt++;
        llvm::Value* b = &*argIt;
        auto* call = builder.CreateCall(hostFn, { a, b }, "hostresult");
        builder.CreateRet(call);
    }

    if (llvm::verifyModule(*module, &llvm::errs())) {
        std::cout << "[lang.jit] selftest: module verification FAILED" << std::endl;
        return false;
    }

    // Run the real O2 pipeline over it -- proves PassBuilder integration
    // works, not just raw codegen.
    RunOptimizationPasses(*module, /*optLevel=*/2);

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        std::cout << "[lang.jit] selftest: LLJIT creation FAILED: " << llvm::toString(jitOrErr.takeError())
                   << std::endl;
        return false;
    }
    impl_->lljit = std::move(*jitOrErr);

    // Register the host trampoline explicitly via absoluteSymbols --
    // deliberately NOT DynamicLibrarySearchGenerator::GetForCurrentProcess(),
    // which on Windows only resolves dllexport'd symbols and would
    // silently fail to find a plain extern "C" function inside an .exe.
    // This is the exact registration mechanism every real intrinsic
    // (GS5) will use.
    llvm::orc::SymbolMap hostSymbols;
    hostSymbols[impl_->lljit->mangleAndIntern("ce_selftest_host_add")] = {
        llvm::orc::ExecutorAddr::fromPtr(&ce_selftest_host_add),
        llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable
    };
    if (auto err = impl_->lljit->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(hostSymbols)))) {
        std::cout << "[lang.jit] selftest: host symbol registration FAILED: " << llvm::toString(std::move(err))
                   << std::endl;
        return false;
    }

    llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
    if (auto err = impl_->lljit->addIRModule(std::move(tsm))) {
        std::cout << "[lang.jit] selftest: addIRModule FAILED: " << llvm::toString(std::move(err)) << std::endl;
        return false;
    }

    auto addSymOrErr = impl_->lljit->lookup("add");
    if (!addSymOrErr) {
        std::cout << "[lang.jit] selftest: lookup(add) FAILED: " << llvm::toString(addSymOrErr.takeError())
                   << std::endl;
        return false;
    }
    auto* addFnPtr = addSymOrErr->toPtr<int64_t (*)(int64_t, int64_t)>();
    const int64_t addResult = addFnPtr(2, 3);

    auto callHostSymOrErr = impl_->lljit->lookup("call_host");
    if (!callHostSymOrErr) {
        std::cout << "[lang.jit] selftest: lookup(call_host) FAILED: "
                   << llvm::toString(callHostSymOrErr.takeError()) << std::endl;
        return false;
    }
    auto* callHostFnPtr = callHostSymOrErr->toPtr<int64_t (*)(int64_t, int64_t)>();
    const int64_t callHostResult = callHostFnPtr(40, 2);

    std::cout << "[lang.jit] selftest: add(2,3)=" << addResult << " call_host(40,2)=" << callHostResult << std::endl;

    return addResult == 5 && callHostResult == 42;
}

ExecResult Runtime::CompileAndRun(Program& program, const std::string& entryPoint, int optLevel) {
    // Find the entry point's declared return type before compiling, so
    // we know which native function-pointer signature to invoke it
    // through after JITing -- LLJIT's lookup() only gives back an
    // address, not a type.
    Type returnType = Type::Unknown;
    bool found = false;
    for (Decl* decl : program.decls) {
        if (decl->kind == DeclKind::Func && decl->funcDecl->name == entryPoint) {
            if (!decl->funcDecl->params.empty()) {
                return ExecResult{ ResultKind::Error, 0, 0.0f, false,
                                    "entry point '" + entryPoint + "' must take zero arguments" };
            }
            returnType = decl->funcDecl->returnType.empty() ? Type::Void : ParseTypeName(decl->funcDecl->returnType);
            found = true;
            break;
        }
    }
    if (!found) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, "no zero-argument function named '" + entryPoint + "'" };
    }
    if (returnType != Type::Int && returnType != Type::Float && returnType != Type::Bool && returnType != Type::Void) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false,
                            "entry point '" + entryPoint + "' returns " + std::string(ToString(returnType)) +
                                ", which --run can't print (only int/float/bool/void are supported)" };
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    std::string buildError;
    auto module = BuildModule(*context, "cel_module", program, buildError);
    if (module == nullptr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, buildError };
    }

    RunOptimizationPasses(*module, optLevel);

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(jitOrErr.takeError()) };
    }
    impl_->lljit = std::move(*jitOrErr);

    llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
    if (auto err = impl_->lljit->addIRModule(std::move(tsm))) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(std::move(err)) };
    }

    auto symOrErr = impl_->lljit->lookup(entryPoint);
    if (!symOrErr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(symOrErr.takeError()) };
    }

    switch (returnType) {
        case Type::Int: {
            auto* fn = symOrErr->toPtr<int64_t (*)()>();
            return ExecResult{ ResultKind::Int, fn(), 0.0f, false, {} };
        }
        case Type::Float: {
            auto* fn = symOrErr->toPtr<float (*)()>();
            return ExecResult{ ResultKind::Float, 0, fn(), false, {} };
        }
        case Type::Bool: {
            // bool crosses the ABI as i1 in this pure-IR (no external
            // call) path, which the C++ calling convention maps to
            // native `bool` cleanly -- the "i32 at the ABI boundary"
            // rule from the plan matters once GS5's extern "C" host
            // trampolines are involved, not here.
            auto* fn = symOrErr->toPtr<bool (*)()>();
            return ExecResult{ ResultKind::Bool, 0, 0.0f, fn(), {} };
        }
        case Type::Void: {
            auto* fn = symOrErr->toPtr<void (*)()>();
            fn();
            return ExecResult{ ResultKind::Void, 0, 0.0f, false, {} };
        }
        default:
            return ExecResult{ ResultKind::Error, 0, 0.0f, false, "internal error: unreachable return type" };
    }
}

std::string Runtime::EmitLLVMIR(Program& program) {
    llvm::LLVMContext context;
    std::string buildError;
    auto module = BuildModule(context, "cel_module", program, buildError);
    if (module == nullptr) {
        return "error: " + buildError;
    }
    std::string ir;
    llvm::raw_string_ostream stream(ir);
    module->print(stream, nullptr);
    return stream.str();
}

} // namespace ce::lang::jit
