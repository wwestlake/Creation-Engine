#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "lang/type.h"

namespace ce::lang::jit {

// One real host-ABI symbol: a name (matching intrinsics.def's cSymbol
// column, or "ce_watchdog_tick" for the loop-budget check module_builder.cpp
// inserts at every loop back-edge) plus the actual function pointer to
// register. Deliberately LLVM-free -- runtime.cpp (the one place that
// already includes the ORC headers) is what turns this into a real
// llvm::orc::SymbolMap via absoluteSymbols; keeping this list itself
// free of llvm/*.h keeps the header firewall (see runtime.h) intact
// even though this file lives under src/jit/.
struct AbiSymbol {
    std::string name;
    void* address = nullptr;
};

// Every real host-ABI trampoline this milestone implements (see
// intrinsics.def's World/Entity/Transform/Debug sections, plus atan2)
// and the watchdog tick function. The 14 "pure computation" math/vec3
// intrinsics never appear here -- GS4 already lowers them directly to
// LLVM IR with no external call at all.
std::vector<AbiSymbol> GetAbiTrampolines();

// GS-Interop: which IntrinsicDomain each real ABI symbol belongs to,
// keyed by cSymbol (the same key GetAbiTrampolines() uses) -- X-macro-
// built directly off intrinsics.def, unlike GetAbiTrampolines() itself
// (a hand-written literal, kept in sync with intrinsics.def only by a
// comment). This is what RegisterAbiTrampolines (abi_registration.cpp)
// filters against for the JIT-symbol-resolution half of capability
// gating (sema.cpp's CheckCall is the compile-time half). A cSymbol
// with no entry here (only "ce_watchdog_tick") is always registered
// regardless of the caller's allowed domain set -- it's a runtime
// safety mechanism, not a script capability.
std::unordered_map<std::string, IntrinsicDomain> GetAbiSymbolDomains();

} // namespace ce::lang::jit
