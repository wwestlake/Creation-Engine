#pragma once

#include <cstdint>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace ce::physics
{

enum class MotionType : std::uint8_t { Static, Kinematic, Dynamic };
enum class ColliderShapeKind : std::uint8_t { Box, Sphere, Capsule };

// Pure configuration data -- a Pod sets this (and ColliderComponent) via
// Schematic nodes; PhysicsWorld's own reconciliation pass (Advance())
// notices an entity carrying both this and ColliderComponent with no live
// Jolt body yet and creates one, writing its handle back into jphBody.
// Replaces the old, dead ce::engine::RigidBody placeholder (never actually
// attached to any entity, see EngineCore/include/engine/gameplay_components.h) --
// that component and its naive gravity/bounce integration in
// FoundationGameplay::Step are removed, not left alongside this.
struct RigidBodyComponent
{
    MotionType motionType = MotionType::Dynamic;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;

    // Invalid (default JPH::BodyID()) until PhysicsWorld's reconciliation
    // pass creates the real Jolt body for this entity.
    JPH::BodyID jphBody;
};

struct ColliderComponent
{
    ColliderShapeKind shape = ColliderShapeKind::Box;
    float halfExtentX = 0.5f;
    float halfExtentY = 0.5f;
    float halfExtentZ = 0.5f;
    float radius = 0.5f;
    float halfHeight = 0.5f;
    std::uint16_t collisionLayer = 0;
    // Possessable Designer Character plan, Phase 2: a sensor collider still
    // fires the existing on_collision* Pod hooks (PhysicsWorld's
    // ContactListener) but applies no physical collision response -- Jolt's
    // own BodyCreationSettings::mIsSensor, threaded straight through by
    // PhysicsWorld's reconciliation pass. "Hit the button, don't get
    // physically blocked by it."
    bool isSensor = false;
};

// Possessable Designer Character plan, Phase 1: a marker so
// PhysicsWorld::AttachToWorld's on_destroy hook can also clean up a live
// JPH::CharacterVirtual (a separate map, keyed by entity, not stored on this
// component itself -- unlike RigidBodyComponent's jphBody, a
// CharacterVirtual is heavier-weight and owned entirely inside PhysicsWorld's
// own Impl). Emplaced by PhysicsWorld::CreateCharacter, never by a Pod
// directly.
struct CharacterControllerTag
{
};

// Post-tick, double-buffered simulation result -- PhysicsWorld::Advance()
// writes here every fixed physics step; PhysicsWorld::InterpolateTransforms()
// reads an alpha-blend of previous/current into the entity's ordinary
// ce::engine::Transform for drawing (Core Architectural Invariant 2: fixed
// 60 Hz physics decoupled from the variable-rate render/UI timer). A Pod
// never reads or writes this directly.
struct PhysicsRenderState
{
    float previousX = 0.0f, previousY = 0.0f, previousZ = 0.0f;
    float previousRotX = 0.0f, previousRotY = 0.0f, previousRotZ = 0.0f;
    float currentX = 0.0f, currentY = 0.0f, currentZ = 0.0f;
    float currentRotX = 0.0f, currentRotY = 0.0f, currentRotZ = 0.0f;
};

} // namespace ce::physics
