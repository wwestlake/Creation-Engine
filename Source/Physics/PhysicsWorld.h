#pragma once

#include "PhysicsComponents.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace ce::engine { class World; }

namespace ce::physics
{

struct RaycastHit
{
    bool hit = false;
    std::int64_t hitEntity = -1;
    float normalX = 0.0f, normalY = 0.0f, normalZ = 0.0f;
    float distance = 0.0f;
};

struct CollisionEvent
{
    enum class Kind { Begin, Persist, End };
    Kind kind = Kind::Begin;
    std::int64_t entityA = -1;
    std::int64_t entityB = -1;
};

// Runtime facts read after CharacterVirtual has resolved its movement. They
// deliberately describe what Jolt produced, not the player input that asked
// for it, so animation and Pods can make the same grounded/airborne decision.
struct CharacterMotionState
{
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float velocityZ = 0.0f;
    bool grounded = false;
};

// Owns the entire Jolt simulation: allocators/job system, the broadphase/
// object-layer boilerplate Jolt requires even for the simplest world, and
// the PhysicsSystem itself. Constructed once, alongside ce::engine::World,
// and lives for the app's whole session -- NEVER owned by a dockable panel
// (Core Architectural Invariant 4: Docking/UI Lifecycle Isolation).
//
// No FRust/node awareness at this layer at all -- entities cross this API
// as plain entt::entity or std::int64_t (matching EngineFrustHost's own
// entity-id convention), never a FRust/PluginRuntime type. See the Jolt
// vendoring plan (Decisions 2-6) for the full rationale.
class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Wires the entt::registry::on_destroy<RigidBodyComponent> safety hook
    // (Decision 6) -- call once, right after constructing both World and
    // PhysicsWorld. Safe to call only once; asserts otherwise.
    void AttachToWorld(ce::engine::World& world);

    // Disconnects registry callbacks and removes the registry context entry
    // before this PhysicsWorld is destroyed or moved to another World.
    void DetachFromWorld();

    // Accumulator-driven fixed step (Core Architectural Invariant 2): call
    // once per UI-timer callback with the REAL elapsed seconds since the
    // last call -- never a hardcoded literal. Internally: runs the body-
    // creation reconciliation pass (Decision 3), pushes kinematic Transform
    // changes into Jolt, steps the fixed 1/60s rate zero or more times
    // (carrying remainder forward), and pulls dynamic results into each
    // entity's PhysicsRenderState double buffer. Returns the leftover
    // accumulator fraction (0..1), the render pass's alpha-interpolation
    // factor between the previous and current physics buffer.
    float Advance(ce::engine::World& world, float realElapsedSeconds);

    // Writes every entity's Transform as an alpha-interpolated blend of its
    // PhysicsRenderState previous/current buffer (Decision 4) -- call once
    // per render frame, after Advance(), passing the alpha it returned.
    // Caller already holds World::RegistryMutex() for its own draw pass;
    // this does not lock it itself.
    void InterpolateTransforms(ce::engine::World& world, float alpha) const;

    // Drains this tick's queued collision begin/persist/end events
    // (Decision 5 / OnCollisionEvent) -- called once per frame by
    // EngineFrustHost::tick(), same cadence as its existing fired-input-
    // combo drain.
    std::vector<CollisionEvent> DrainCollisionEvents();

    // A single, cached raycast query -- RaycastQuery's multiple Schematic
    // output pins (hit/entity/normal/distance) all read this same cached
    // result via separate getter calls, since one FFI call can only ever
    // return one scalar (Decision 5's note on the single-scalar-return
    // constraint).
    RaycastHit CastRay(float originX, float originY, float originZ,
                       float dirX, float dirY, float dirZ, float maxDistance);

    // Each takes World& (not stored) purely to validate the entity id and
    // look up its RigidBodyComponent -- a no-op if the entity has no body
    // yet (matches the reconciliation pass in Advance() being the only
    // place a body actually comes into existence).
    void ApplyForce(ce::engine::World& world, std::int64_t entity, float x, float y, float z);
    void ApplyImpulse(ce::engine::World& world, std::int64_t entity, float x, float y, float z);
    void SetLinearVelocity(ce::engine::World& world, std::int64_t entity, float x, float y, float z);
    void GetLinearVelocity(ce::engine::World& world, std::int64_t entity, float& outX, float& outY, float& outZ) const;

    // Possessable Designer Character plan, Phase 1: JPH::CharacterVirtual --
    // a manually-driven kinematic controller, deliberately separate from
    // RigidBodyComponent/ColliderComponent above. Not tracked by
    // PhysicsSystem itself (Jolt's own design), so UpdateCharacter() must be
    // called once per tick for every live character, same discipline as
    // Advance() for rigid bodies but a fully separate code path -- replaces
    // FreeCamera's fake-gravity/hardcoded-box firstPersonMode_ placeholder.
    bool CreateCharacter(ce::engine::World& world, std::int64_t entity,
                        float capsuleRadius, float capsuleHalfHeight,
                        float maxSlopeAngleDegrees = 50.0f);
    void DestroyCharacter(std::int64_t entity);
    // desiredHorizontalX/Z: world-space horizontal move intent, already
    // scaled to the desired speed. jumpSpeed: positive to jump THIS call if
    // currently grounded, 0 otherwise. Writes the entity's Transform.position
    // (not rotation -- facing direction is the possessing Pod/camera's own
    // concern, not this controller's).
    void UpdateCharacter(ce::engine::World& world, std::int64_t entity,
                        float desiredHorizontalX, float desiredHorizontalZ,
                        float jumpSpeed, float dt);
    bool IsCharacterGrounded(std::int64_t entity) const;
    [[nodiscard]] CharacterMotionState GetCharacterMotionState(std::int64_t entity) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ce::engine::World* attachedWorld_ = nullptr;
    entt::scoped_connection rigidBodyDestroyConnection_;
    entt::scoped_connection characterDestroyConnection_;
};

} // namespace ce::physics
