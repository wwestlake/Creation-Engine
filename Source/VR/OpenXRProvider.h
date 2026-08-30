#pragma once

#include "engine/vr.h"

namespace ce::vr {

// PC OpenXR runtime discovery. Graphics-session creation is separate because
// it must be bound to the editor's active OpenGL surface.
class OpenXRProvider final : public engine::vr::Provider {
public:
    OpenXRProvider() = default;
    ~OpenXRProvider() override;

    bool initialize() override;
    void shutdown() override;
    engine::vr::SessionState sessionState() const noexcept override { return state_; }
    engine::vr::RenderTargetSize recommendedRenderSize() const noexcept override { return { 1440, 1600 }; }
    bool beginFrame(engine::vr::FrameState& frame) override;
    bool submitFrame(const engine::vr::FrameState& frame) override;

private:
    engine::vr::SessionState state_ = engine::vr::SessionState::unavailable;
    void* instance_ = nullptr;
    void* system_ = nullptr;
};

} // namespace ce::vr
