#pragma once

#include <vector>

#include <JuceHeader.h>

#include "Render/Scene/Animation.h"
#include "Scene/Components.h"

namespace ce::scene {

// Samples `clip` at `time` (seconds) and returns one LOCAL transform per
// joint in `skeleton`, in the same order as skeleton.joints. A joint/path
// the clip doesn't animate keeps that joint's own bind-pose value (see
// Skeleton::Joint::bindTranslation/bindRotation/bindScale), so a clip
// that only animates a subset of joints -- or only some of a joint's
// three channels -- still produces a complete, correct pose rather than
// snapping the rest to identity.
//
// Rotation is interpolated with NLERP (normalized lerp), not a true
// SLERP -- a common real-time-playback simplification, visually
// indistinguishable from SLERP for the rotation deltas one keyframe
// interval usually spans. CubicSpline channels are sampled by lerping
// between each key's VALUE sample only, ignoring the in/out tangents
// glTF also stores for them -- a real Hermite evaluation is a
// straightforward follow-up if a cubic-spline-authored clip ever visibly
// needs it; every clip tested so far (including the vendored SimpleSkin
// animation) uses linear interpolation.
std::vector<juce::Matrix3D<float>> SampleLocalTransforms(const AnimationClip& clip, float time,
                                                          const Skeleton& skeleton);

// Walks skeleton.joints' parent-index hierarchy (any array order, not
// just parent-before-child -- glTF doesn't guarantee that) composing the
// given LOCAL transforms (from SampleLocalTransforms above, or the bind
// pose directly) into WORLD transforms per joint, then right-multiplies
// each by its own inverseBindMatrix to produce the final mesh-space
// skinning matrix -- the actual "bone matrix palette" the skinning
// shader indexes into.
std::vector<juce::Matrix3D<float>> ComputeSkinningMatrices(const Skeleton& skeleton,
                                                            const std::vector<juce::Matrix3D<float>>& localTransforms);

} // namespace ce::scene
