#pragma once

#include <memory>

namespace ce::lang::jit {

// Opaque JIT runtime -- owns an LLVM ORC LLJIT instance internally. No
// llvm/*.h type ever appears in this header, and none may be added to
// it: every LLVM include is confined to Language/src/jit/*.cpp, so
// nothing that includes this header (including JUCE-based code, once
// this links into the editor/server in GS6) can collide with LLVM's own
// headers/macros. This is a structural firewall, not a convention to
// remember.
class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // GS1 self-test: hand-builds a tiny LLVM IR module (an `add`
    // function, and a `call_host` function that calls back into a host
    // trampoline registered the same way every real intrinsic will be
    // registered from GS5 onward), runs it through the O2 pass
    // pipeline, verifies it, JITs it, and executes both functions,
    // checking their results. This is the concrete, checkable proof
    // that vcpkg-installed LLVM actually links and JITs in this exact
    // toolchain -- the reason GS1 exists.
    bool RunSelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ce::lang::jit
