#pragma once

#include <memory>

#include "engine/script_runtime.h"

namespace ce::lang::jit {

// Constructs the real ce_lang_jit-backed ce::engine::IScriptRuntime --
// the one concrete implementation CreationEngineEditor/
// CreationEngineServer (and celc, for its own headless verification)
// each construct once at startup and inject via
// ce::engine::World::SetScriptRuntime, so EngineCore's Simulation::Step
// can compile and run CEL scripts without EngineCore itself ever
// depending on LLVM. This is a plain function returning the interface
// type (not a class) so this header -- like runtime.h -- stays
// LLVM-free; the concrete CelScriptRuntime class is private to
// script_runtime.cpp.
std::shared_ptr<ce::engine::IScriptRuntime> CreateScriptRuntime();

} // namespace ce::lang::jit
