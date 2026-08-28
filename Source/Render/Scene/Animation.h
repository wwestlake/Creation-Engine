#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

namespace ce {

// Matches CE_MAX_BONES in library/skinning.glsl -- the skinning shader's
// uBoneMatrices[] array is fixed-size, same reasoning as kMaxPointLights
// in Light.h.
constexpr int kMaxBones = 64;

enum class AnimationInterpolation { Step, Linear, CubicSpline };

// One glTF-style animation channel: drives a single joint's translation,
// rotation, or scale over time. Rotation values are quaternions (x,y,z,w);
// translation/scale are vec3. For CubicSpline channels, `values` stores 3x
// the per-key components (in-tangent, value, out-tangent) in that order,
// matching glTF's own accessor layout so extraction doesn't have to
// reshape anything -- see AnimationSampler.cpp for how (or, today, how
// little) the tangents are actually used.
struct AnimationChannel {
    enum class Path { Translation, Rotation, Scale };

    int jointIndex = -1; // index into the target Skeleton::joints.
    Path path = Path::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;  // keyframe times in seconds, strictly increasing.
    std::vector<float> values; // componentsPerKey(path) floats per key (x3 for CubicSpline).
};

struct AnimationClip {
    juce::String name;
    float duration = 0.0f; // latest keyframe time across all channels.
    std::vector<AnimationChannel> channels;

    // AI6: present only when root motion extraction was enabled at import
    // time -- the root joint's translation channel moves here out of
    // `channels`, since it drives the entity's world position during
    // playback instead of the joint's own local pose (which then stays at
    // its bind-pose translation). See Scene/AnimationSlicer.h's
    // ExtractRootMotion for how this gets populated.
    std::optional<AnimationChannel> rootMotion;
};

} // namespace ce
