#pragma once

#include <cstdint>
#include <string>

#include "engine/world.h"

namespace ce::engine {

// The opaque object every compiled CEL function and intrinsic call
// implicitly receives as its first parameter (see
// docs/SCRIPTING_ABI.md) -- not a thread-local, so multiple Worlds (or
// multiple concurrent compilations) stay independent and reentrant.
// Callers are expected to hold `world->RegistryMutex()` for the whole
// duration any compiled CEL code runs against a given context --
// intrinsics (Language/src/jit/intrinsic_trampolines.cpp) never lock it
// themselves, per the ABI's rule 6.
struct ScriptContext {
    World* world = nullptr;

    // Elapsed simulation time (seconds), advanced by the host driver
    // (celc's --run-world, later GS6's Simulation::Step) by a fixed `dt`
    // once per tick -- what the `world_time()` intrinsic reads. Lives
    // here rather than on World itself since World's own Tick counter is
    // an integer with no fixed real-world time unit attached to it.
    float elapsedTime = 0.0f;

    // The runaway-script watchdog: decremented once per loop iteration
    // executed by any compiled CEL code using this context (a check
    // inserted at every while/for loop back-edge by IR-gen). When it
    // reaches zero the running script faults with CEL9001 instead of
    // hanging the caller. Not a security sandbox -- just a guard
    // against an accidental infinite loop; see docs/SCRIPTING_ABI.md.
    int64_t loopBudget = 10'000'000;
    bool faulted = false;
    std::string faultMessage;
};

} // namespace ce::engine
