#include "Render/Scene/FreeCamera.h"

#include <cmath>

namespace ce {

juce::Vector3D<float> FreeCamera::Forward() const {
    return { std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_) };
}

juce::Vector3D<float> FreeCamera::FlatForward() const {
    return { std::sin(yaw_), 0.0f, -std::cos(yaw_) };
}

void FreeCamera::Update(float deltaSeconds, juce::Rectangle<int> viewportScreenBounds) {
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const auto mousePosInt = juce::Desktop::getMousePosition();
    const bool isLooking = mods.isRightButtonDown() && viewportScreenBounds.contains(mousePosInt);
    const auto mousePos = mousePosInt.toFloat();

    if (isLooking) {
        if (hasLastMousePos_) {
            const auto delta = mousePos - lastMousePos_;
            constexpr float sensitivity = 0.005f;
            yaw_ += delta.x * sensitivity;
            pitch_ = juce::jlimit(-1.5f, 1.5f, pitch_ - delta.y * sensitivity);
        }
        lastMousePos_ = mousePos;
        hasLastMousePos_ = true;
    } else {
        hasLastMousePos_ = false;
    }

    const auto flatForward = FlatForward();
    const auto right = flatForward ^ juce::Vector3D<float>{ 0.0f, 1.0f, 0.0f };

    if (isLooking) {
        const float speed = 3.0f * deltaSeconds * speedMultiplier_.load(std::memory_order_relaxed);
        if (juce::KeyPress::isKeyCurrentlyDown('W')) {
            position_ += flatForward * speed;
        }
        if (juce::KeyPress::isKeyCurrentlyDown('S')) {
            position_ -= flatForward * speed;
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
}

void FreeCamera::AdjustSpeed(float wheelDeltaY) {
    const float current = speedMultiplier_.load(std::memory_order_relaxed);
    const float updated = juce::jlimit(0.1f, 10.0f, current * (1.0f + wheelDeltaY * 0.2f));
    speedMultiplier_.store(updated, std::memory_order_relaxed);
}

} // namespace ce
