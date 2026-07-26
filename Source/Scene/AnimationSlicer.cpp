#include "Scene/AnimationSlicer.h"

#include "Scene/AnimationSampler.h"

namespace ce::scene {

namespace {

AnimationChannel SliceChannel(const AnimationChannel& source, float startTime, float endTime) {
    AnimationChannel sliced;
    sliced.jointIndex = source.jointIndex;
    sliced.path = source.path;
    // CubicSpline sources are downgraded to Linear here -- only the
    // boundary-sampled start/end values are kept below (plain values,
    // not tangent triplets), so the sliced channel can't claim to still
    // be CubicSpline.
    sliced.interpolation =
        source.interpolation == AnimationInterpolation::CubicSpline ? AnimationInterpolation::Linear
                                                                      : source.interpolation;

    const int components = source.path == AnimationChannel::Path::Rotation ? 4 : 3;

    const auto startValues = SampleChannelValues(source, startTime);
    const auto endValues = SampleChannelValues(source, endTime);
    if (startValues.empty() || endValues.empty()) {
        return sliced; // source channel had no keys at all -- return an empty (harmless) channel.
    }

    sliced.times.push_back(0.0f);
    sliced.values.insert(sliced.values.end(), startValues.begin(), startValues.end());

    // Preserve original in-between keyframes for Step/Linear sources --
    // for CubicSpline sources these would need the tangent triplet
    // layout, which the downgrade above already discards, so only the
    // boundary samples survive for those.
    if (source.interpolation != AnimationInterpolation::CubicSpline) {
        for (std::size_t k = 0; k < source.times.size(); ++k) {
            const float t = source.times[k];
            if (t > startTime && t < endTime) {
                sliced.times.push_back(t - startTime);
                const float* v = &source.values[k * static_cast<std::size_t>(components)];
                sliced.values.insert(sliced.values.end(), v, v + components);
            }
        }
    }

    sliced.times.push_back(endTime - startTime);
    sliced.values.insert(sliced.values.end(), endValues.begin(), endValues.end());

    return sliced;
}

} // namespace

std::vector<AnimationClip> SliceClip(const AnimationClip& source, const std::vector<ClipSlice>& slices) {
    std::vector<AnimationClip> result;
    result.reserve(slices.size());

    for (const auto& slice : slices) {
        const float start = juce::jlimit(0.0f, source.duration, slice.startTime);
        const float end = juce::jlimit(start, source.duration, slice.endTime);
        if (end <= start) {
            continue;
        }

        AnimationClip clip;
        clip.name = slice.name;
        clip.duration = end - start;
        clip.channels.reserve(source.channels.size());
        for (const auto& channel : source.channels) {
            clip.channels.push_back(SliceChannel(channel, start, end));
        }
        result.push_back(std::move(clip));
    }

    return result;
}

bool ExtractRootMotion(AnimationClip& clip, int rootJointIndex) {
    for (auto it = clip.channels.begin(); it != clip.channels.end(); ++it) {
        if (it->jointIndex == rootJointIndex && it->path == AnimationChannel::Path::Translation) {
            clip.rootMotion = *it;
            clip.channels.erase(it);
            return true;
        }
    }
    return false;
}

} // namespace ce::scene
