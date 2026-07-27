#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <entt/entt.hpp>

namespace ce::engine {

// Forward-declared only -- IScriptRuntime's real definition
// (engine/script_runtime.h) is a pure abstract interface EngineCore
// owns but never implements; the concrete implementation lives in
// Language/src/jit (which links EngineCore, so the dependency can't run
// the other way). World only ever holds a shared_ptr to one, which
// works fine against an incomplete type.
class IScriptRuntime;

// Authoritative tick counter every state change is timestamped against
// (spec section 2.3). Both the server's simulation and the editor's
// play-in-viewport mode advance the same World type.
using Tick = std::uint64_t;

// Thin wrapper around the entity registry so call sites depend on
// ce::engine::World rather than entt directly — keeps the ECS library an
// implementation detail that can change without rippling through the
// codebase.
//
// entt::registry is not internally thread-safe — concurrent access from
// two threads (even a read on one racing a write on another) is
// undefined behavior, not just "probably fine." As of SC3, more than one
// thread genuinely touches the registry: the editor's render thread
// (drawing/animating placed entities) and the message thread (UI panels
// reading/editing the scene). RegistryMutex() is the one lock every such
// access — read or write, from any caller — must hold for its duration.
// std::mutex rather than juce::CriticalSection because EngineCore stays
// framework-agnostic (see the class comment below) — plain std::mutex
// works from both JUCE call sites and non-JUCE ones alike.
class World {
public:
    entt::registry& Registry() { return registry_; }
    const entt::registry& Registry() const { return registry_; }
    std::mutex& RegistryMutex() { return registryMutex_; }

    entt::entity CreateEntity() { return registry_.create(); }
    void DestroyEntity(entt::entity e) { registry_.destroy(e); }

    // tick_ is read from the render thread (driving tick-based scene
    // animation) and written from the message thread (the editor's
    // play/pause timer) — atomic so that's well-defined without needing
    // a full lock just for a monotonic counter. Relaxed ordering is
    // enough: nothing else is synchronized through this value.
    Tick CurrentTick() const { return tick_.load(std::memory_order_relaxed); }
    Tick AdvanceTick() { return tick_.fetch_add(1, std::memory_order_relaxed) + 1; }
    void ResetTick() { tick_.store(0, std::memory_order_relaxed); }

    // GS6: injected once at startup by whichever executable links both
    // EngineCore and ce_lang_jit (CreationEngineEditor,
    // CreationEngineServer construct one ce::lang::jit::CelScriptRuntime
    // each and call this) -- Simulation::Step reads it from here to run
    // ScriptComponents without EngineCore itself ever depending on LLVM.
    // A World with no runtime set (ScriptRuntime() == nullptr) simply
    // never executes scripts -- headless tools with no scripting need
    // (or that haven't reached script setup yet) don't have to construct
    // a no-op stand-in.
    void SetScriptRuntime(std::shared_ptr<IScriptRuntime> runtime) { scriptRuntime_ = std::move(runtime); }
    IScriptRuntime* ScriptRuntime() const { return scriptRuntime_.get(); }

    // Mirrors AdvanceTick()'s exact pattern, but for real elapsed
    // seconds rather than an integer tick count -- what
    // ScriptContext::elapsedTime is seeded from at the start of every
    // Simulation::Step (a fresh ScriptContext is built each Step call,
    // so this is what makes world_time() keep advancing correctly
    // across ticks regardless).
    float AdvanceScriptTime(float dt) {
        scriptTime_ += dt;
        return scriptTime_;
    }
    float CurrentScriptTime() const { return scriptTime_; }

private:
    entt::registry registry_;
    std::mutex registryMutex_;
    std::atomic<Tick> tick_{ 0 };
    std::shared_ptr<IScriptRuntime> scriptRuntime_;
    float scriptTime_ = 0.0f;
};

} // namespace ce::engine
