#include "Scene/AnimationSampler.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace ce::scene {

namespace {

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Quatf {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

Vec3f LerpVec3(const float* a, const float* b, float t) {
    return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };
}

// Normalized lerp -- flips b onto a's hemisphere first (shortest path),
// like every real-time NLERP implementation, then renormalizes.
Quatf NlerpQuat(const float* a, const float* b, float t) {
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) {
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }

    Quatf r{ a[0] + (bx - a[0]) * t, a[1] + (by - a[1]) * t, a[2] + (bz - a[2]) * t, a[3] + (bw - a[3]) * t };
    const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (len > 1e-8f) {
        r.x /= len;
        r.y /= len;
        r.z /= len;
        r.w /= len;
    }
    return r;
}

// Column-major TRS composition, matching the layout cgltf_node_transform_
// local + juce::Matrix3D<float>(float*) already use elsewhere in this
// codebase (GltfLoader's ExtractSkin) -- translation in the last column,
// rotation folded into the first three (scaled by s).
juce::Matrix3D<float> ComposeTRS(const Vec3f& t, const Quatf& q, const Vec3f& s) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    float m[16] = { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f,
                     2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f,
                     2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f,
                     t.x, t.y, t.z, 1.0f };

    m[0] *= s.x; m[1] *= s.x; m[2] *= s.x;
    m[4] *= s.y; m[5] *= s.y; m[6] *= s.y;
    m[8] *= s.z; m[9] *= s.z; m[10] *= s.z;

    return juce::Matrix3D<float>(m);
}

void ApplySample(AnimationChannel::Path path, const float* v, Vec3f& t, Quatf& r, Vec3f& s) {
    switch (path) {
        case AnimationChannel::Path::Translation:
            t = { v[0], v[1], v[2] };
            break;
        case AnimationChannel::Path::Scale:
            s = { v[0], v[1], v[2] };
            break;
        case AnimationChannel::Path::Rotation:
            r = { v[0], v[1], v[2], v[3] };
            break;
    }
}

// Finds the keyframe segment `time` falls in. Times before the first key
// clamp to key 0 (t=0, no extrapolation); at/after the last key clamp to
// the last key (index = count-1, t=0) -- both mean "hold the nearest
// authored pose," the conventional glTF playback behavior outside a
// clip's authored range.
void FindKeyframe(const std::vector<float>& times, float time, int& outIndex, float& outT) {
    if (times.empty()) {
        outIndex = -1;
        outT = 0.0f;
        return;
    }
    if (time <= times.front()) {
        outIndex = 0;
        outT = 0.0f;
        return;
    }
    if (time >= times.back()) {
        outIndex = static_cast<int>(times.size()) - 1;
        outT = 0.0f;
        return;
    }

    const auto it = std::upper_bound(times.begin(), times.end(), time);
    const int next = static_cast<int>(it - times.begin());
    const int prev = next - 1;
    const float span = times[static_cast<std::size_t>(next)] - times[static_cast<std::size_t>(prev)];
    outIndex = prev;
    outT = span > 1e-8f ? (time - times[static_cast<std::size_t>(prev)]) / span : 0.0f;
}

} // namespace

std::vector<juce::Matrix3D<float>> SampleLocalTransforms(const AnimationClip& clip, float time,
                                                          const Skeleton& skeleton) {
    const std::size_t jointCount = skeleton.joints.size();
    std::vector<Vec3f> translations(jointCount);
    std::vector<Quatf> rotations(jointCount);
    std::vector<Vec3f> scales(jointCount);

    for (std::size_t i = 0; i < jointCount; ++i) {
        const auto& joint = skeleton.joints[i];
        translations[i] = { joint.bindTranslation.x, joint.bindTranslation.y, joint.bindTranslation.z };
        rotations[i] = { joint.bindRotation[0], joint.bindRotation[1], joint.bindRotation[2], joint.bindRotation[3] };
        scales[i] = { joint.bindScale.x, joint.bindScale.y, joint.bindScale.z };
    }

    for (const auto& channel : clip.channels) {
        if (channel.jointIndex < 0 || static_cast<std::size_t>(channel.jointIndex) >= jointCount) {
            continue;
        }

        int keyIndex = -1;
        float t = 0.0f;
        FindKeyframe(channel.times, time, keyIndex, t);
        if (keyIndex < 0) {
            continue;
        }

        const int components = channel.path == AnimationChannel::Path::Rotation ? 4 : 3;
        const bool cubic = channel.interpolation == AnimationInterpolation::CubicSpline;
        const int stride = components * (cubic ? 3 : 1);
        const int valueOffset = cubic ? components : 0; // skip the in-tangent for cubic-spline keys.

        const auto sampleAt = [&](int index) -> const float* {
            return &channel.values[static_cast<std::size_t>(index) * static_cast<std::size_t>(stride) +
                                    static_cast<std::size_t>(valueOffset)];
        };

        const bool isLastKey = keyIndex == static_cast<int>(channel.times.size()) - 1;
        const float* a = sampleAt(keyIndex);
        auto& jointTranslation = translations[static_cast<std::size_t>(channel.jointIndex)];
        auto& jointRotation = rotations[static_cast<std::size_t>(channel.jointIndex)];
        auto& jointScale = scales[static_cast<std::size_t>(channel.jointIndex)];

        if (channel.interpolation == AnimationInterpolation::Step || isLastKey || t <= 0.0f) {
            ApplySample(channel.path, a, jointTranslation, jointRotation, jointScale);
            continue;
        }

        const float* b = sampleAt(keyIndex + 1);
        switch (channel.path) {
            case AnimationChannel::Path::Translation:
                jointTranslation = LerpVec3(a, b, t);
                break;
            case AnimationChannel::Path::Scale:
                jointScale = LerpVec3(a, b, t);
                break;
            case AnimationChannel::Path::Rotation:
                jointRotation = NlerpQuat(a, b, t);
                break;
        }
    }

    std::vector<juce::Matrix3D<float>> result;
    result.reserve(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        result.push_back(ComposeTRS(translations[i], rotations[i], scales[i]));
    }
    return result;
}

std::vector<juce::Matrix3D<float>> ComputeSkinningMatrices(const Skeleton& skeleton,
                                                            const std::vector<juce::Matrix3D<float>>& localTransforms) {
    const std::size_t jointCount = skeleton.joints.size();
    std::vector<juce::Matrix3D<float>> worldTransforms(jointCount);
    std::vector<bool> computed(jointCount, false);

    // Recursive rather than a linear pass over the array -- glTF's skin
    // joint order isn't guaranteed parent-before-child, so a linear pass
    // could read an as-yet-uncomputed parent's identity placeholder.
    // Memoized, so each joint's world transform is still only computed
    // once; safe from infinite recursion since a skin's joint hierarchy
    // is a tree (no cycles) by construction.
    std::function<const juce::Matrix3D<float>&(std::size_t)> resolveWorld =
        [&](std::size_t index) -> const juce::Matrix3D<float>& {
        if (computed[index]) {
            return worldTransforms[index];
        }
        const int parent = skeleton.joints[index].parentIndex;
        worldTransforms[index] = (parent >= 0 && static_cast<std::size_t>(parent) < jointCount)
                                      ? resolveWorld(static_cast<std::size_t>(parent)) * localTransforms[index]
                                      : localTransforms[index];
        computed[index] = true;
        return worldTransforms[index];
    };

    for (std::size_t i = 0; i < jointCount; ++i) {
        resolveWorld(i);
    }

    std::vector<juce::Matrix3D<float>> skinningMatrices(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        skinningMatrices[i] = worldTransforms[i] * skeleton.joints[i].inverseBindMatrix;
    }
    return skinningMatrices;
}

} // namespace ce::scene
