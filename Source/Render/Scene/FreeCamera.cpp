#include "Render/Scene/FreeCamera.h"

#include <cmath>

namespace ce {

FreeCamera::FreeCamera(juce::Component& viewport) : viewport_(viewport) {
    viewport_.addMouseListener(this, false);
}

FreeCamera::~FreeCamera() {
    viewport_.removeMouseListener(this);
}

juce::Vector3D<float> FreeCamera::Forward() const {
    const float yaw = yaw_.load(std::memory_order_relaxed);
    const float pitch = pitch_.load(std::memory_order_relaxed);
    return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch) };
}

juce::Vector3D<float> FreeCamera::FlatForward() const {
    const float yaw = yaw_.load(std::memory_order_relaxed);
    return { std::sin(yaw), 0.0f, -std::cos(yaw) };
}

void FreeCamera::mouseDown(const juce::MouseEvent& event) {
    if (!event.mods.isRightButtonDown()) {
        return;
    }

    isLooking_.store(true, std::memory_order_relaxed);
    lastDragScreenPos_ = event.getScreenPosition().toFloat();
    viewport_.setMouseCursor(juce::MouseCursor::NoCursor);
    event.source.enableUnboundedMouseMovement(true);
}

void FreeCamera::mouseDrag(const juce::MouseEvent& event) {
    if (!isLooking_.load(std::memory_order_relaxed)) {
        return;
    }

    const auto currentPos = event.getScreenPosition().toFloat();
    const auto delta = currentPos - lastDragScreenPos_;
    lastDragScreenPos_ = currentPos;

    constexpr float sensitivity = 0.005f;
    yaw_.store(yaw_.load(std::memory_order_relaxed) + delta.x * sensitivity, std::memory_order_relaxed);
    pitch_.store(
        juce::jlimit(-1.5f, 1.5f, pitch_.load(std::memory_order_relaxed) - delta.y * sensitivity),
        std::memory_order_relaxed);
}

void FreeCamera::mouseUp(const juce::MouseEvent& event) {
    if (event.mods.isRightButtonDown()) {
        return; // a different button was released; still right-dragging.
    }
    if (!isLooking_.exchange(false, std::memory_order_relaxed)) {
        return; // wasn't looking, nothing to clean up.
    }

    viewport_.setMouseCursor(juce::MouseCursor::NormalCursor);
    event.source.enableUnboundedMouseMovement(false);
}

void FreeCamera::Update(float deltaSeconds) {
    if (!isLooking_.load(std::memory_order_relaxed)) {
        return;
    }

    // isKeyCurrentlyDown polls real OS key state, not JUCE focus/event
    // routing — without this check, WASD typed into some other app
    // entirely (this chat, a browser, whatever has focus) still moves the
    // camera as long as isLooking_ is (still) true. isForegroundProcess()
    // is JUCE's own mechanism for "is my app actually the active one right
    // now" — exactly the guard needed here.
    if (!juce::Process::isForegroundProcess()) {
        return;
    }

    // W/S fly along the camera's actual look direction (pitch included) —
    // looking up and pressing W climbs, not just walks-while-level. A/D
    // strafing stays derived from the flat (yaw-only) forward, since
    // strafing shouldn't gain a vertical component just because you're
    // looking up or down.
    const auto forward = Forward();
    const auto flatForward = FlatForward();
    const auto right = flatForward ^ juce::Vector3D<float>{ 0.0f, 1.0f, 0.0f };
    const float speed = 3.0f * deltaSeconds * speedMultiplier_.load(std::memory_order_relaxed);

    if (juce::KeyPress::isKeyCurrentlyDown('W')) {
        position_ += forward * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('S')) {
        position_ -= forward * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('A')) {
        position_ -= right * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('D')) {
        position_ += right * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('E')) {
        position_.y += speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('Q')) {
        position_.y -= speed;
    }
}

void FreeCamera::AdjustSpeed(float wheelDeltaY) {
    const float current = speedMultiplier_.load(std::memory_order_relaxed);
    const float updated = juce::jlimit(0.1f, 10.0f, current * (1.0f + wheelDeltaY * 0.2f));
    speedMultiplier_.store(updated, std::memory_order_relaxed);
}

} // namespace ce
