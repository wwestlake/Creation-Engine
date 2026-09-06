#pragma once

namespace ce::scene
{
struct Animator;
}

namespace ce::scene
{

// Shared crossfade logic -- extracted so both EngineFrustHost::animCrossfadeTo
// (the Domain::Animation Schematic node's host-extern) and PossessedCharacter
// (Possessable Designer Character plan, Phase 3 -- C++ orchestration, not a
// Pod, per the approved plan) drive the exact same Animator state machine
// rather than maintaining two independently-drifting copies of it. Pure
// component manipulation -- no registry lookup, no locking; callers already
// hold whatever lock is appropriate for their own call site.
//
// Returns false if clipName isn't found in animator.clips (or clips is
// null) -- true otherwise, including the "already the active clip, no-op"
// case (matches animCrossfadeTo's existing contract exactly).
bool CrossfadeAnimatorTo(Animator& animator, const char* clipName, int blendMillis);

} // namespace ce::scene
