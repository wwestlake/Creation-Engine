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

    std::cout << "celc -- Creation Engine Language compiler/test-driver\n"
                 "usage:\n"
                 "  celc --selftest-jit\n"
                 "  celc --dump-ast <file.cel>\n"
                 "  celc --check <file.cel>\n";
    return 1;
}
