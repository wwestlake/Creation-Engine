#pragma once

#include <vector>

#include <JuceHeader.h>

#include "Render/Scene/Animation.h"

namespace ce::scene {

// One requested named clip, expressed as a [startTime, endTime) window
// (seconds) into a source clip's timeline -- AI6's answer to "one glTF
// Animation actually contains several logical clips back to back," a
// common export pattern this engine had no way to express before.
struct ClipSlice {
    juce::String name;
    float startTime = 0.0f;
    float endTime = 0.0f;
};

// Builds one new AnimationClip per requested slice, trimming and
// rebasing `source`'s channels to each window. Boundary values (at
// exactly startTime and endTime) are computed by actually sampling the
// source channel there (see AnimationSampler::SampleChannelValues), not
// just truncating to the nearest authored keyframe -- so a slice's very
// first/last frame is the correct interpolated pose even when a
// keyframe doesn't happen to land exactly on the cut. Slices with
// endTime <= startTime (after clamping to the source's duration) are
// skipped. A CubicSpline-interpolated source channel is downgraded to
// Linear in the sliced result -- re-deriving correct tangents for a
// trimmed cubic curve's new endpoints is out of scope, and every clip
// this engine has actually extracted so far is authored Linear anyway.
std::vector<AnimationClip> SliceClip(const AnimationClip& source, const std::vector<ClipSlice>& slices);

// AI6: moves `rootJointIndex`'s Translation channel out of clip.channels
// into clip.rootMotion, if it has one. Returns false (no-op) if that
// joint has no translation channel in this clip. Playback (see
// ViewportComponent) then leaves the root joint's local pose at bind
// translation and instead applies clip.rootMotion additively to the
// entity's own Transform, so the entity actually moves through the
// world instead of the root joint sliding within local skeleton space.
bool ExtractRootMotion(AnimationClip& clip, int rootJointIndex);

} // namespace ce::scene
