#pragma once

#include "engine/math.h"

namespace ce::engine
{

// Input is deliberately framework-neutral. JUCE, a gamepad backend, a
// network client, or FRust can all produce the same per-tick command.
struct GameplayInput
{
    float moveX = 0.0f;
    float moveZ = 0.0f;
    bool jumpPressed = false;
};

struct CharacterMotor
{
    Vec3 spawnPosition{};
    float moveSpeed = 4.0f;
    float jumpSpeed = 5.0f;
    float gravity = -9.8f;
    float verticalVelocity = 0.0f;
    float deathHeight = -5.0f;
    bool grounded = false;
    bool alive = true;
};

struct RigidBody
{
    Vec3 velocity{};
    float radius = 0.5f;
    float gravity = -9.8f;
    float restitution = 0.65f;
    bool dynamic = true;
};

} // namespace ce::engine
