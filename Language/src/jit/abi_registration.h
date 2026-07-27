#pragma once

#include <llvm/ExecutionEngine/Orc/Core.h>

namespace llvm::orc {
class LLJIT;
}

namespace ce::lang::jit {

// Registers every real host-ABI trampoline (intrinsic_trampolines.cpp)
// plus the watchdog tick function into `lljit`'s main JITDylib via
// absoluteSymbols -- explicit registration, deliberately NOT
// DynamicLibrarySearchGenerator::GetForCurrentProcess() (see runtime.cpp's
// GS1 selftest comment for why that fails on Windows for non-dllexport'd
// .exe symbols). Shared by runtime.cpp's CompileAndRun/RunWorldProgram
// and script_runtime.cpp's CelScriptRuntime::Compile -- every one of
// them JITs a module that can call these same symbols, so there's one
// registration helper rather than three copies drifting apart.
llvm::Error RegisterAbiTrampolines(llvm::orc::LLJIT& lljit);

} // namespace ce::lang::jit
