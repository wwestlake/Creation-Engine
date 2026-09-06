#include "PossessedCharacter.h"

#include "Physics/PhysicsWorld.h"
#include "Input/InputActionSystem.h"
#include "Scene/AnimatorControl.h"
#include "Scene/Components.h"
#include "engine/world.h"

#include <entt/entt.hpp>

#include <cmath>
#include <mutex>

namespace ce::character
{

void PossessedCharacter::Update(ce::engine::World& world, ce::physics::PhysicsWorld& physics,
                                const ce::input::InputActionSystem& input,
                                std::int64_t entity, float forwardYawRadians, float dt)
{
    // Move/Jump/Sprint/Crouch already exist in InputActionSystem's own
    // starter preset -- reused as-is, no new bindings needed for movement
    // (Decision 4). Crouch is read but not yet applied to movement speed/
    // capsule height -- named, not built, matching Phase 3's own scope.
    const float forwardIntent = (input.IsActionActive("MoveForward") ? 1.0f : 0.0f)
                               - (input.IsActionActive("MoveBackward") ? 1.0f : 0.0f);
    const float rightIntent = (input.IsActionActive("MoveRight") ? 1.0f : 0.0f)
                             - (input.IsActionActive("MoveLeft") ? 1.0f : 0.0f);
    const bool sprinting = input.IsActionActive("Sprint");
    const bool jumpPressed = input.WasActionPressed("Jump");

    float speed = 0.0f;
    float worldX = 0.0f;
    float worldZ = 0.0f;
    const float intentLengthSq = forwardIntent * forwardIntent + rightIntent * rightIntent;
    if (intentLengthSq > 0.0001f)
    {
        const float intentLength = std::sqrt(intentLengthSq);
        const float normalizedForward = forwardIntent / intentLength;
        const float normalizedRight = rightIntent / intentLength;
        speed = sprinting ? kRunSpeed : kWalkSpeed;

        // Rotate the (forward, right) intent by the camera's own yaw --
        // standard third-/first-person "move relative to where you're
        // looking" convention, not a world-space-fixed direction.
        const float sinYaw = std::sin(forwardYawRadians);
        const float cosYaw = std::cos(forwardYawRadians);
        worldX = (normalizedForward * sinYaw + normalizedRight * cosYaw) * speed;
        worldZ = (normalizedForward * cosYaw - normalizedRight * sinYaw) * speed;
    }

    physics.UpdateCharacter(world, entity, worldX, worldZ, jumpPressed ? kJumpSpeed : 0.0f, dt);

    // Animation: discrete Idle/Walk/Run crossfades on speed thresholds
    // (Decision 5 -- no blend tree, matches what already ships).
    const char* targetClip = speed <= 0.01f ? "Idle" : (sprinting ? "Run" : "Walk");
    {
        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        auto& registry = world.Registry();
        const auto entityHandle = static_cast<entt::entity>(entity);
        if (auto* animator = registry.valid(entityHandle) ? registry.try_get<scene::Animator>(entityHandle) : nullptr)
            scene::CrossfadeAnimatorTo(*animator, targetClip, 200);
    }
}

} // namespace ce::character
