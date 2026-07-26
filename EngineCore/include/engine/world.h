#pragma once

#include <atomic>
#include <cstdint>

#include <entt/entt.hpp>

namespace ce::engine {

// Authoritative tick counter every state change is timestamped against
// (spec section 2.3). Both the server's simulation and the editor's
// play-in-viewport mode advance the same World type.
using Tick = std::uint64_t;

// Thin wrapper around the entity registry so call sites depend on
// ce::engine::World rather than entt directly — keeps the ECS library an
// implementation detail that can change without rippling through the
// codebase.
class World {
public:
    entt::registry& Registry() { return registry_; }
    const entt::registry& Registry() const { return registry_; }

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

private:
    entt::registry registry_;
    std::atomic<Tick> tick_{ 0 };
};

} // namespace ce::engine
