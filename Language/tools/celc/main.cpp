#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "lang/ast_printer.h"
#include "lang/compiler.h"
#include "lang/diagnostics.h"
#include "lang/jit/runtime.h"
#include "lang/sema.h"

namespace {

int RunDumpAst(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }

    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(file, arena, diagnostics);

    diagnostics.PrintAll(std::cerr);

    if (program == nullptr || diagnostics.HasErrors()) {
        return 1;
    }

    ce::lang::PrintAst(*program, std::cout);
    return 0;
}

// --check: parse + semantic analysis, no codegen (that's GS4). Kept as
// its own subcommand rather than folded into --dump-ast, since
// --dump-ast's output is diffed against fixtures written before sema
// existed -- running sema there too would mean every parse fixture also
// has to be a well-typed program, which isn't the property those
// fixtures are testing.
int RunCheck(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }

    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(file, arena, diagnostics);
    if (program == nullptr || diagnostics.HasErrors()) {
        diagnostics.PrintAll(std::cerr);
        return 1;
    }

    const bool ok = ce::lang::AnalyzeProgram(*program, diagnostics);
    diagnostics.PrintAll(std::cerr);
    if (!ok) {
        return 1;
    }

    std::cout << "OK" << std::endl;
    return 0;
}

// Parses+checks `path`, returning the resulting Program (owned by
// `arena`, which callers must keep alive for as long as they use the
// result), or nullptr with diagnostics already printed on failure.
// Shared by --run and --emit-llvm, which both need a fully-checked
// program before they can touch codegen.
ce::lang::Program* ParseAndCheck(const std::string& path, ce::lang::AstArena& arena,
                                  ce::lang::DiagnosticEngine& diagnostics) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return nullptr;
    }
    ce::lang::Program* program = ce::lang::ParseProgram(file, arena, diagnostics);
    if (program == nullptr || diagnostics.HasErrors()) {
        diagnostics.PrintAll(std::cerr);
        return nullptr;
    }
    if (!ce::lang::AnalyzeProgram(*program, diagnostics)) {
        diagnostics.PrintAll(std::cerr);
        return nullptr;
    }
    return program;
}

// --run <file.cel> [--entry NAME] [--opt N]: compiles to LLVM IR,
// optimizes at level N (default 2), JITs it, and calls the
// zero-argument function `NAME` (default "main"), printing its result.
// GS4's exec tests run every fixture at both -O0 and -O2 (see
// Language/tests/run_exec_test.cmake) specifically to check the
// optimizer didn't change program *behavior*, not just performance.
int RunExec(const std::string& path, const std::string& entryPoint, int optLevel) {
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ParseAndCheck(path, arena, diagnostics);
    if (program == nullptr) {
        return 1;
    }

    ce::lang::jit::Runtime runtime;
    const ce::lang::jit::ExecResult result = runtime.CompileAndRun(*program, entryPoint, optLevel);

    switch (result.kind) {
        case ce::lang::jit::ResultKind::Int:
            std::cout << result.intValue << std::endl;
            return 0;
        case ce::lang::jit::ResultKind::Float:
            std::cout << result.floatValue << std::endl;
            return 0;
        case ce::lang::jit::ResultKind::Bool:
            std::cout << (result.boolValue ? "true" : "false") << std::endl;
            return 0;
        case ce::lang::jit::ResultKind::Void:
            std::cout << "(void)" << std::endl;
            return 0;
        case ce::lang::jit::ResultKind::Error:
            std::cerr << "celc: " << result.errorMessage << std::endl;
            return 1;
    }
    return 1;
}

int RunEmitLLVM(const std::string& path) {
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ParseAndCheck(path, arena, diagnostics);
    if (program == nullptr) {
        return 1;
    }

    ce::lang::jit::Runtime runtime;
    std::cout << runtime.EmitLLVMIR(*program);
    return 0;
}

} // namespace

// celc -- the CEL compiler/test-driver binary. This is the project's
// single headless entry point into the Language module: every GS
// milestone from here on adds a subcommand (--dump-ast, --run,
// --run-world, --graph-to-source, ...) rather than spinning up a
// separate test target per milestone.
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--selftest-jit") {
        ce::lang::jit::Runtime runtime;
        const bool ok = runtime.RunSelfTest();
        std::cout << (ok ? "[celc] selftest-jit: PASS" : "[celc] selftest-jit: FAIL") << std::endl;
        return ok ? 0 : 1;
    }

    if (argc >= 3 && std::string(argv[1]) == "--dump-ast") {
        return RunDumpAst(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--check") {
        return RunCheck(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--run") {
        std::string entryPoint = "main";
        int optLevel = 2;
        for (int i = 3; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            if (flag == "--entry") {
                entryPoint = argv[i + 1];
            } else if (flag == "--opt") {
                optLevel = std::atoi(argv[i + 1]);
            }
        }
        return RunExec(argv[2], entryPoint, optLevel);
    }

    if (argc >= 3 && std::string(argv[1]) == "--emit-llvm") {
        return RunEmitLLVM(argv[2]);
    }

    std::cout << "celc -- Creation Engine Language compiler/test-driver\n"
                 "usage:\n"
                 "  celc --selftest-jit\n"
                 "  celc --dump-ast <file.cel>\n"
                 "  celc --check <file.cel>\n"
                 "  celc --run <file.cel> [--entry NAME] [--opt 0-3]\n"
                 "  celc --emit-llvm <file.cel>\n";
    return 1;
}
