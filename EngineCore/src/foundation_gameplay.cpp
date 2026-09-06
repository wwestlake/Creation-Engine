#include "engine/foundation_gameplay.h"

#include "engine/core_components.h"
#include "engine/world.h"

#include <algorithm>

namespace ce::engine
{

void FoundationGameplay::Step(World& world, const GameplayInput& input, float dt)
{
    if (dt <= 0.0f)
        return;

    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const float clampedDt = std::min(dt, 0.1f);
    auto view = world.Registry().view<Transform, CharacterMotor>();
    for (const auto entity : view)
    {
        auto& transform = view.get<Transform>(entity);
        auto& motor = view.get<CharacterMotor>(entity);

        if (!motor.alive)
            continue;

        transform.position.x += input.moveX * motor.moveSpeed * clampedDt;
        transform.position.z += input.moveZ * motor.moveSpeed * clampedDt;

        if (input.jumpPressed && motor.grounded)
        {
            motor.verticalVelocity = motor.jumpSpeed;
            motor.grounded = false;
        }

        motor.verticalVelocity += motor.gravity * clampedDt;
        transform.position.y += motor.verticalVelocity * clampedDt;

        if (transform.position.y <= 0.0f)
        {
            transform.position.y = 0.0f;
            motor.verticalVelocity = 0.0f;
            motor.grounded = true;
        }

        if (transform.position.y < motor.deathHeight)
        {
            transform.position = motor.spawnPosition;
            motor.verticalVelocity = 0.0f;
            motor.grounded = true;
        }
    }
    // Real rigid-body physics is ce::physics::PhysicsWorld now (Source/
    // Physics/) -- see MainComponent::timerCallback's own
    // physicsWorld_.Advance() call, run alongside this Step(). The naive
    // gravity/floor-bounce integration that used to live here (over a
    // RigidBody component nothing ever actually attached to an entity) is
    // gone, not left running alongside the real thing.
}

} // namespace ce::engine
