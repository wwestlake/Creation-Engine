#include "abi_registration.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include "intrinsic_trampolines.h"

namespace ce::lang::jit {

llvm::Error RegisterAbiTrampolines(llvm::orc::LLJIT& lljit) {
    llvm::orc::SymbolMap symbols;
    for (const AbiSymbol& sym : GetAbiTrampolines()) {
        symbols[lljit.mangleAndIntern(sym.name)] = { llvm::orc::ExecutorAddr::fromPtr(sym.address),
                                                      llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable };
    }
    return lljit.getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols)));
}

} // namespace ce::lang::jit
