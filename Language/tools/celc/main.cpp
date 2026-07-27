#include <iostream>
#include <string>

#include "lang/jit/runtime.h"

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

    std::cout << "celc -- Creation Engine Language compiler/test-driver\n"
                 "usage: celc --selftest-jit\n";
    return 1;
}
