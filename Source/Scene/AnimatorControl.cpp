#include "AnimatorControl.h"

#include "Components.h"

#include <juce_core/juce_core.h>

namespace ce::scene
{

bool CrossfadeAnimatorTo(Animator& animator, const char* clipName, int blendMillis)
{
    if (clipName == nullptr || *clipName == '\0' || animator.clips == nullptr)
        return false;

    const auto& clips = *animator.clips;
    for (std::size_t i = 0; i < clips.size(); ++i)
    {
        if (clips[i].name != clipName)
            continue;

        if (animator.activeClip == static_cast<int>(i) && animator.blendFromClip < 0)
            return true; // already the active clip and not mid-blend -- a no-op, not an error.

        // If a previous crossfade is still in progress, its still-blending-
        // FROM clip is deliberately dropped here in favor of the current
        // (still-blending-TO) clip -- restarting a blend mid-blend loses
        // that earlier clip's contribution rather than composing three
        // clips together. Accepted simplification, not a bug (matches
        // EngineFrustHost::animCrossfadeTo's own original comment on this).
        animator.blendFromClip = animator.activeClip;
        animator.blendFromTime = animator.time;
        animator.activeClip = static_cast<int>(i);
        animator.time = 0.0f;
        animator.blendTime = 0.0f;
        animator.blendDuration = juce::jmax<float>(0.0f, static_cast<float>(blendMillis) / 1000.0f);
        return true;
    }
    return false;
}

} // namespace ce::scene
