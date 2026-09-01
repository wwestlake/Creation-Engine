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
    return { std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_) };
}

juce::Vector3D<float> FreeCamera::FlatForward() const {
    return { std::sin(yaw_), 0.0f, -std::cos(yaw_) };
}

// JUCE-side: entry only. Nothing here reads mouse position or touches
// the cursor -- Update() takes over entirely once isLooking_ flips true.
void FreeCamera::mouseDown(const juce::MouseEvent& event) {
    viewport_.grabKeyboardFocus();
    if (!event.mods.isRightButtonDown()) return;
    isLooking_.store(true, std::memory_order_relaxed);
}

void FreeCamera::Update(float deltaSeconds) {
    const bool flaggedLooking = isLooking_.load(std::memory_order_relaxed);

    if (flaggedLooking && !wasLookingLastFrame_) {
        // Just entered (mouseDown set the flag since last frame): hide
        // the cursor and anchor it at the viewport centre as this
        // frame's reference point for the delta below.
        viewport_.setMouseCursor(juce::MouseCursor::NoCursor);
        lastMouseScreenPos_ = viewport_.getScreenBounds().getCentre().toFloat();
        juce::Desktop::getInstance().setMousePosition(lastMouseScreenPos_.toInt());
    } else if (flaggedLooking && !juce::ModifierKeys::getCurrentModifiersRealtime().isRightButtonDown()) {
        // Still flagged as looking, but the OS says the button is
        // actually up -- the real release, caught here instead of
        // waiting on a JUCE mouseUp that might not arrive while the
        // cursor is hidden. Nothing to restore: yaw_/pitch_/position_
        // just stop changing, the camera is already exactly where it is.
        isLooking_.store(false, std::memory_order_relaxed);
        viewport_.setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    const bool isLooking = isLooking_.load(std::memory_order_relaxed);
    if (isLooking) {
        const auto currentPos = juce::Desktop::getMousePosition().toFloat();
        const auto delta = currentPos - lastMouseScreenPos_;
        constexpr float sensitivity = 0.005f;
        yaw_ += delta.x * sensitivity;
        pitch_ = juce::jlimit(-1.5f, 1.5f, pitch_ - delta.y * sensitivity);
        // Re-anchor to the viewport centre every frame so the cursor
        // never actually reaches a real screen edge during a long look.
        const auto centre = viewport_.getScreenBounds().getCentre().toFloat();
        juce::Desktop::getInstance().setMousePosition(centre.toInt());
        lastMouseScreenPos_ = centre;
    }
    wasLookingLastFrame_ = isLooking;

    const bool firstPerson = firstPersonMode_.load(std::memory_order_relaxed);
    if (!firstPerson && !isLooking) {
        return;
    }

    // isKeyCurrentlyDown polls real OS key state, not JUCE focus/event
    // routing -- without this check, WASD typed into some other app
    // entirely (this chat, a browser, whatever has focus) still moves the
    // camera as long as isLooking_ is (still) true. isForegroundProcess()
    // is JUCE's own mechanism for "is my app actually the active one right
    // now" -- exactly the guard needed here.
    if (!juce::Process::isForegroundProcess()) {
        return;
    }
    if (firstPerson && !viewport_.hasKeyboardFocus(true)) {
        return;
    }

    // W/S fly along the camera's actual look direction (pitch included) --
    // looking up and pressing W climbs, not just walks-while-level. A/D
    // strafing stays derived from the flat (yaw-only) forward, since
    // strafing shouldn't gain a vertical component just because you're
    // looking up or down.
    const auto forward = Forward();
    const auto flatForward = FlatForward();
    const auto right = flatForward ^ juce::Vector3D<float>{ 0.0f, 1.0f, 0.0f };
    const float speed = (firstPerson ? 4.0f : 3.0f) * deltaSeconds * speedMultiplier_.load(std::memory_order_relaxed);

    if (juce::KeyPress::isKeyCurrentlyDown('W')) {
        position_ += (firstPerson ? flatForward : forward) * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('S')) {
        position_ -= (firstPerson ? flatForward : forward) * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('A')) {
        position_ -= right * speed;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('D')) {
        position_ += right * speed;
    }
    if (!firstPerson && juce::KeyPress::isKeyCurrentlyDown('E')) {
        position_.y += speed;
    }
    if (!firstPerson && juce::KeyPress::isKeyCurrentlyDown('Q')) {
        position_.y -= speed;
    }

    if (firstPerson) {
        // The starter room is an axis-aligned 20x20 test space. The camera
        // is the player capsule for this first slice; the bounds become
        // proper collider components when the physics layer lands.
        position_.x = juce::jlimit(-9.35f, 9.35f, position_.x);
        position_.z = juce::jlimit(-9.35f, 9.35f, position_.z);
        position_.y -= 9.8f * deltaSeconds;
        if (position_.y <= 1.6f)
            position_.y = 1.6f;
        if (position_.y < -5.0f)
            position_ = { 0.0f, 1.6f, 5.0f };
    }
}

void FreeCamera::AdjustSpeed(float wheelDeltaY) {
    const float current = speedMultiplier_.load(std::memory_order_relaxed);
    const float updated = juce::jlimit(0.1f, 10.0f, current * (1.0f + wheelDeltaY * 0.2f));
    speedMultiplier_.store(updated, std::memory_order_relaxed);
}

} // namespace ce
