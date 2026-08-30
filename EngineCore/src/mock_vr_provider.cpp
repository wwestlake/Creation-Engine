#include "engine/mock_vr_provider.h"

#include <cmath>

namespace ce::engine::vr {

bool MockProvider::initialize()
{
    state_ = SessionState::ready;
    frameIndex_ = 0;
    elapsedSeconds_ = 0.0f;
    return true;
}

void MockProvider::shutdown()
{
    state_ = SessionState::unavailable;
}

bool MockProvider::beginFrame(FrameState& frame)
{
    if (state_ == SessionState::lost || state_ == SessionState::unavailable) {
        return false;
    }

    state_ = SessionState::running;
    frame = {};
    frame.frameIndex = ++frameIndex_;
    frame.predictedDisplayTimeSeconds = static_cast<double>(elapsedSeconds_) + (1.0 / 90.0);

    constexpr std::array<float, 16> identityProjection {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    frame.leftEye.projection = identityProjection;
    frame.rightEye.projection = identityProjection;

    const float sway = std::sin(elapsedSeconds_ * 1.5f) * 0.025f;
    frame.leftEye.pose.position = {-0.032f + sway, 1.65f, 0.0f};
    frame.rightEye.pose.position = {0.032f + sway, 1.65f, 0.0f};
    frame.leftController.connected = true;
    frame.rightController.connected = true;
    frame.leftController.pose.position = {-0.28f + sway, 1.35f, -0.35f};
    frame.rightController.pose.position = {0.28f + sway, 1.35f, -0.35f};
    return true;
}

bool MockProvider::submitFrame(const FrameState& frame)
{
    return state_ == SessionState::running && frame.frameIndex == frameIndex_;
}

void MockProvider::advance(float seconds)
{
    if (seconds > 0.0f) {
        elapsedSeconds_ += seconds;
    }
}

void MockProvider::setSessionLost(bool lost) noexcept
{
    state_ = lost ? SessionState::lost : SessionState::ready;
}

} // namespace ce::engine::vr
