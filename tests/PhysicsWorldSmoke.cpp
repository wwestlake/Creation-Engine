#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsComponents.h"

#include "engine/core_components.h"
#include "engine/world.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// Phase 5 of the Jolt vendoring plan: a real, end-to-end proof, not just
// "it compiles" -- a static ground box and a dynamic sphere placed above
// it, both configured purely through the RigidBodyComponent/
// ColliderComponent pair a Schematic's config nodes would set (see
// PhysicsComponents.h), driven through many real PhysicsWorld::Advance()
// calls, asserting the sphere actually falls and settles to rest on the
// ground -- and that an OnCollisionEvent-shaped contact-begin event
// actually fires when it lands.

namespace {
void Fail(const char* message) {
    std::fprintf(stderr, "PhysicsWorldSmoke failure: %s\n", message);
    std::exit(1);
}
} // namespace

int main() {
    ce::engine::World world;
    ce::physics::PhysicsWorld physics;
    physics.AttachToWorld(world);

    // Static ground: a flat box centered at the origin, top surface at y=0.
    const auto ground = world.Registry().create();
    {
        auto& transform = world.Registry().emplace<ce::engine::Transform>(ground);
        transform.position = { 0.0f, -1.0f, 0.0f };
        auto& rigidBody = world.Registry().emplace<ce::physics::RigidBodyComponent>(ground);
        rigidBody.motionType = ce::physics::MotionType::Static;
        auto& collider = world.Registry().emplace<ce::physics::ColliderComponent>(ground);
        collider.shape = ce::physics::ColliderShapeKind::Box;
        collider.halfExtentX = 50.0f;
        collider.halfExtentY = 1.0f;
        collider.halfExtentZ = 50.0f;
    }

    // Dynamic sphere, dropped from well above the ground.
    const auto ballEntity = world.Registry().create();
    {
        auto& transform = world.Registry().emplace<ce::engine::Transform>(ballEntity);
        transform.position = { 0.0f, 10.0f, 0.0f };
        auto& rigidBody = world.Registry().emplace<ce::physics::RigidBodyComponent>(ballEntity);
        rigidBody.motionType = ce::physics::MotionType::Dynamic;
        rigidBody.mass = 1.0f;
        rigidBody.restitution = 0.1f; // low bounce -- settles quickly for a short, deterministic test.
        auto& collider = world.Registry().emplace<ce::physics::ColliderComponent>(ballEntity);
        collider.shape = ce::physics::ColliderShapeKind::Sphere;
        collider.radius = 0.5f;
    }

    bool sawCollisionBegin = false;
    // 5 real seconds of simulation, advanced in the same "real elapsed
    // seconds per callback" shape MainComponent::timerCallback will use --
    // several fixed 1/60s steps happen inside each Advance() call.
    for (int frame = 0; frame < 150; ++frame) {
        const float alpha = physics.Advance(world, 1.0f / 30.0f);
        (void) alpha;
        for (const auto& event : physics.DrainCollisionEvents()) {
            if (event.kind == ce::physics::CollisionEvent::Kind::Begin)
                sawCollisionBegin = true;
        }
    }
    physics.InterpolateTransforms(world, 1.0f);

    if (!sawCollisionBegin)
        Fail("expected the falling sphere to fire a collision-begin event against the ground.");

    const auto& transform = world.Registry().get<ce::engine::Transform>(ballEntity);
    // Expected resting height: ground top (y=0) + sphere radius (0.5).
    const float expectedRestY = 0.5f;
    const float error = std::fabs(transform.position.y - expectedRestY);
    if (error > 0.1f) {
        std::fprintf(stderr, "sphere did not settle at the expected height: got y=%.4f, expected ~%.4f\n",
                     transform.position.y, expectedRestY);
        return 1;
    }
    if (std::fabs(transform.position.x) > 0.01f || std::fabs(transform.position.z) > 0.01f) {
        Fail("sphere drifted horizontally with no lateral force ever applied -- unexpected.");
    }

    std::printf("PhysicsWorldSmoke passed: sphere fell, collided (event fired), and settled at y=%.4f.\n",
                transform.position.y);
    return 0;
}
