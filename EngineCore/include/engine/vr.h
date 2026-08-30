#pragma once

#include <array>
#include <cstdint>

#include "engine/math.h"

namespace ce::engine::vr {

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Pose {
    Vec3 position{};
    Quaternion orientation{};
};

enum class SessionState : std::uint8_t {
    unavailable,
    ready,
    running,
    stopping,
    lost
};

enum class Eye : std::uint8_t { left, right };

struct View {
    Pose pose{};
    std::array<float, 16> projection{};
};

struct ControllerState {
    bool connected = false;
    Pose pose{};
    bool selectPressed = false;
    bool gripPressed = false;
    bool menuPressed = false;
    Vec3 thumbstick{};
};

struct RenderTargetSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct FrameState {
    std::uint64_t frameIndex = 0;
    double predictedDisplayTimeSeconds = 0.0;
    View leftEye{};
    View rightEye{};
    ControllerState leftController{};
    ControllerState rightController{};
};

// Engine-owned boundary for desktop, mock, and OpenXR providers. No renderer
// or application code should depend directly on a vendor SDK.
class Provider {
public:
    virtual ~Provider() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual SessionState sessionState() const noexcept = 0;
    virtual RenderTargetSize recommendedRenderSize() const noexcept = 0;
    virtual bool beginFrame(FrameState& frame) = 0;
    virtual bool submitFrame(const FrameState& frame) = 0;
};

} // namespace ce::engine::vr
