#pragma once

#include "engine/vr.h"

namespace ce::engine::vr {

// Deterministic provider used before hardware is available and for automated
// stereo/action tests. It models a standing headset and two controllers.
class MockProvider final : public Provider {
public:
    bool initialize() override;
    void shutdown() override;
    SessionState sessionState() const noexcept override { return state_; }
    RenderTargetSize recommendedRenderSize() const noexcept override { return { 1440, 1600 }; }
    bool beginFrame(FrameState& frame) override;
    bool submitFrame(const FrameState& frame) override;

    void advance(float seconds);
    void setSessionLost(bool lost) noexcept;
    void setHandsTracked(bool tracked) noexcept { handsTracked_ = tracked; }
    void setControllersConnected(bool connected) noexcept { controllersConnected_ = connected; }
    [[nodiscard]] float elapsedSeconds() const noexcept { return elapsedSeconds_; }

private:
    SessionState state_ = SessionState::unavailable;
    std::uint64_t frameIndex_ = 0;
    float elapsedSeconds_ = 0.0f;
    bool handsTracked_ = true;
    bool controllersConnected_ = true;
};

} // namespace ce::engine::vr
