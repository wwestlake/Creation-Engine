#pragma once

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

    Tick CurrentTick() const { return tick_; }
    Tick AdvanceTick() { return ++tick_; }

private:
    entt::registry registry_;
    Tick tick_ = 0;
};

} // namespace ce::engine
