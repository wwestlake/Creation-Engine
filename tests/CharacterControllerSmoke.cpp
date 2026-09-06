#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsComponents.h"

#include "engine/core_components.h"
#include "engine/world.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// Possessable Designer Character plan, Phase 5: proves the two halves of
// Jolt's CharacterVirtual integration that matter for "hit the button, don't
// get physically blocked by it" -- (1) a sensor collider lets the character
// walk through it while still firing a collision event Pods can react to,
// and (2) a normal (non-sensor) collider actually blocks/redirects the
// character, so button colliders aren't silently non-solid by accident.

namespace {
void Fail(const char* message) {
    std::fprintf(stderr, "CharacterControllerSmoke failure: %s\n", message);
    std::exit(1);
}

// Creates a static box collider (sensor or solid) at the given position and
// returns its entity -- same RigidBodyComponent+ColliderComponent pattern
// PhysicsWorldSmoke already uses for its ground box.
entt::entity MakeStaticBox(ce::engine::World& world, float x, float y, float z,
                          float halfX, float halfY, float halfZ, bool isSensor) {
    const auto entity = world.Registry().create();
    auto& transform = world.Registry().emplace<ce::engine::Transform>(entity);
    transform.position = { x, y, z };
    auto& rigidBody = world.Registry().emplace<ce::physics::RigidBodyComponent>(entity);
    rigidBody.motionType = ce::physics::MotionType::Static;
    auto& collider = world.Registry().emplace<ce::physics::ColliderComponent>(entity);
    collider.shape = ce::physics::ColliderShapeKind::Box;
    collider.halfExtentX = halfX;
    collider.halfExtentY = halfY;
    collider.halfExtentZ = halfZ;
    collider.isSensor = isSensor;
    return entity;
}
} // namespace

int main() {
    // --- Part 1: sensor wall -- character passes through, event still fires. ---
    {
        ce::engine::World world;
        ce::physics::PhysicsWorld physics;
        physics.AttachToWorld(world);

        const auto character = world.Registry().create();
        world.Registry().emplace<ce::engine::Transform>(character).position = { 0.0f, 1.0f, 0.0f };
        if (!physics.CreateCharacter(world, static_cast<std::int64_t>(entt::to_integral(character)), 0.3f, 0.9f))
            Fail("CreateCharacter failed");

        // Ground plane so the character actually walks (stays grounded) on
        // its way to the wall instead of free-falling away from the wall's
        // vertical range before ever reaching it in Z.
        MakeStaticBox(world, 0.0f, -1.0f, 2.0f, 10.0f, 1.0f, 10.0f, /*isSensor*/ false);
        const auto sensorWall = MakeStaticBox(world, 0.0f, 1.0f, 3.0f, 2.0f, 2.0f, 0.2f, /*isSensor*/ true);
        (void) sensorWall;

        bool sawSensorContact = false;
        for (int i = 0; i < 240; ++i) {
            physics.Advance(world, 1.0f / 30.0f); // reconciles the static boxes' real Jolt bodies, steps rigid bodies.
            physics.UpdateCharacter(world, static_cast<std::int64_t>(entt::to_integral(character)),
                                    0.0f, 2.0f, 0.0f, 1.0f / 30.0f); // walk toward +Z at 2 m/s.
            for (const auto& event : physics.DrainCollisionEvents())
                if (event.kind == ce::physics::CollisionEvent::Kind::Begin)
                    sawSensorContact = true;
        }

        if (!sawSensorContact)
            Fail("expected a collision-begin event when the character walked through the sensor wall.");

        const auto& transform = world.Registry().get<ce::engine::Transform>(character);
        if (transform.position.z < 4.0f)
            Fail("character did not pass through the sensor wall -- it appears to have been physically blocked.");

        std::printf("Part 1 passed: character passed through the sensor wall (z=%.2f) and fired a contact event.\n",
                    transform.position.z);
    }

    // --- Part 2: solid wall -- character is blocked, does not pass through. ---
    {
        ce::engine::World world;
        ce::physics::PhysicsWorld physics;
        physics.AttachToWorld(world);

        const auto character = world.Registry().create();
        world.Registry().emplace<ce::engine::Transform>(character).position = { 0.0f, 1.0f, 0.0f };
        if (!physics.CreateCharacter(world, static_cast<std::int64_t>(entt::to_integral(character)), 0.3f, 0.9f))
            Fail("CreateCharacter failed");

        MakeStaticBox(world, 0.0f, -1.0f, 2.0f, 10.0f, 1.0f, 10.0f, /*isSensor*/ false);
        MakeStaticBox(world, 0.0f, 1.0f, 3.0f, 2.0f, 2.0f, 2.0f, /*isSensor*/ false);

        for (int i = 0; i < 240; ++i) {
            physics.Advance(world, 1.0f / 30.0f);
            physics.UpdateCharacter(world, static_cast<std::int64_t>(entt::to_integral(character)),
                                    0.0f, 2.0f, 0.0f, 1.0f / 30.0f);
        }

        const auto& transform = world.Registry().get<ce::engine::Transform>(character);
        // Wall's near face is at z = 3 - 2 - 0.2(margin) = ~0.8; character
        // capsule radius 0.3 means it should stop well short of z=2.5.
        if (transform.position.z > 2.5f)
            Fail("character passed through a non-sensor wall -- it should have been physically blocked.");

        std::printf("Part 2 passed: character was blocked by the solid wall (z=%.2f, did not reach z=3).\n",
                    transform.position.z);
    }

    std::printf("CharacterControllerSmoke passed: sensor vs. solid collider behavior correctly distinguished.\n");
    return 0;
}
