#pragma once

#include <llvm/ExecutionEngine/Orc/Core.h>

#include "lang/type.h"

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
//
// `allowed` (GS-Interop, default All()) filters which symbols actually
// get registered against GetAbiSymbolDomains() -- a symbol outside it
// is simply never given an address in this JITDylib, so a call to it
// fails as a genuine JIT symbol-lookup error, not just a convention
// violation. This is the defense-in-depth half of capability gating;
// sema.cpp's CheckCall is the compile-time half (and is expected to
// already have rejected the same call, so this filter is normally
// never actually exercised on the success path -- see the GS-Interop
// plan for why both layers still matter). ce_watchdog_tick (absent
// from GetAbiSymbolDomains()) is always registered regardless of
// `allowed` -- it's a runtime safety mechanism, not a script capability.
llvm::Error RegisterAbiTrampolines(llvm::orc::LLJIT& lljit, const IntrinsicDomainSet& allowed = IntrinsicDomainSet::All());

} // namespace ce::lang::jit
