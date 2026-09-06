#pragma once

#include <cstdint>

namespace ce::engine { class World; }
namespace ce::physics { class PhysicsWorld; }
namespace ce::input { class InputActionSystem; }

namespace ce::character
{

// Possessable Designer Character plan, Phase 3: owns the move-intent ->
// PhysicsWorld::UpdateCharacter -> Animator-crossfade pipeline for one
// possessed entity. Plain C++ orchestration, not a Pod -- per the approved
// plan, this is infrastructure the possession/testing loop needs (reusing
// InputActionSystem's already-existing Move/Jump/Sprint/Crouch preset and
// the existing two-way animation crossfade, per Decisions 4-5), not a
// gameplay behavior a Pod author would write.
//
// Distinct from ce::character::CharacterDefinition (Character/
// CharacterDefinition.h) -- that is a pure authoring-data template asset
// (a character "sheet"); this is the live runtime state for whichever
// entity is currently possessed.
class PossessedCharacter
{
public:
    // forwardYawRadians: the possessing camera's own yaw -- movement is
    // relative to where the player is looking (standard third-/first-
    // person convention), not world-space-fixed. Reads Move/Jump/Sprint
    // actions from `input` (already polled this tick by
    // InputActionSystem::PollOncePerFrame, same as everything else that
    // reads it), calls PhysicsWorld::UpdateCharacter for movement, and
    // crossfades Idle/Walk/Run based on the resulting horizontal speed.
    void Update(ce::engine::World& world, ce::physics::PhysicsWorld& physics,
               const ce::input::InputActionSystem& input,
               std::int64_t entity, float forwardYawRadians, float dt);

private:
    // Real-world speed thresholds, not authored-per-entity yet (Phase 3
    // scope) -- a Pod-configurable version is natural future work once
    // this proves out.
    static constexpr float kWalkSpeed = 2.0f;
    static constexpr float kRunSpeed = 5.5f;
    static constexpr float kJumpSpeed = 4.5f;
};

} // namespace ce::character
