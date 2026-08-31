#pragma once

#include "engine/vr.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ce::vr {

// PC OpenXR runtime discovery. Graphics-session creation is separate because
// it must be bound to the editor's active OpenGL surface.
class OpenXRProvider final : public engine::vr::Provider {
public:
    OpenXRProvider() = default;
    ~OpenXRProvider() override;

    bool initialize() override;
    // Must be called from the thread with the editor's current OpenGL
    // context. The raw context is the HGLRC returned by JUCE.
    bool initializeOpenGL(void* rawOpenGLContext);
    void shutdown() override;
    engine::vr::SessionState sessionState() const noexcept override { return state_; }
    engine::vr::RenderTargetSize recommendedRenderSize() const noexcept override { return renderSize_; }
    bool beginFrame(engine::vr::FrameState& frame) override;
    bool submitFrame(const engine::vr::FrameState& frame) override;

private:
    engine::vr::SessionState state_ = engine::vr::SessionState::unavailable;
    void* instance_ = nullptr;
    void* system_ = nullptr;
    void* session_ = nullptr;
    void* space_ = nullptr;
    engine::vr::RenderTargetSize renderSize_{ 1440, 1600 };
    std::array<std::uint64_t, 2> swapchains_{};
    std::array<std::vector<std::uint32_t>, 2> swapchainImages_{};
    std::array<std::uint32_t, 2> acquiredImages_{};
    std::array<std::uint32_t, 2> framebuffers_{};
    std::array<float, 8> fov_{};
    std::array<bool, 2> imageAcquired_{};
    std::int64_t displayTime_ = 0;
    bool frameBegun_ = false;

    bool pollEvents();
    bool endFrameWithoutLayers();
};

} // namespace ce::vr
