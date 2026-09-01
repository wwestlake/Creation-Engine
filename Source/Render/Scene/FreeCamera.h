#pragma once

#include <atomic>

#include <JuceHeader.h>

namespace ce {

// Fly camera: hold the right mouse button over the viewport to look
// around (mouse delta -> yaw/pitch) and use WASD to move, Q/E for
// down/up -- the standard "hold right-click to fly" convention.
//
// Two different systems own the mouse at two different times, and the
// handoff between them is deliberate, not incidental: before fly mode
// starts, JUCE owns it -- mouseDown (below) is a normal JUCE event and
// is how entry is detected, same as any other click in the app. Once
// flying, the mouse is effectively under OpenGL's control (cursor
// hidden, re-anchored every frame from inside the render loop), and
// JUCE's own event delivery for it can't be trusted from there --
// specifically, mouseUp for the release was unreliable while the
// cursor was hidden (an earlier version of this class relied on it,
// and releasing right-click sometimes never exited fly mode at all).
// So release detection, and the continuous look-around itself, are
// polled every frame from Update() (the render thread) via
// ModifierKeys::getCurrentModifiersRealtime(), which asks the OS
// directly rather than waiting on a JUCE event that might not arrive.
//
// isLooking_ is set true by mouseDown (message thread) and set false by
// Update() (render thread) -- a real cross-thread write in each
// direction, but a rare, single one-shot flip each way, not a
// contended, high-frequency lock, so a plain atomic<bool> is enough, no
// mutex needed. yaw_/pitch_/position_/lastMouseScreenPos_ are touched
// only from Update(), so they stay plain members.
class FreeCamera final : private juce::MouseListener {
public:
    explicit FreeCamera(juce::Component& viewport);
    ~FreeCamera() override;

    // Call once per frame from the render thread -- polls real mouse/key
    // state and advances position_/yaw_/pitch_ accordingly.
    void Update(float deltaSeconds);
    void AdjustSpeed(float wheelDeltaY);
    void EnableFirstPersonMode() { firstPersonMode_.store(true, std::memory_order_relaxed); }

    juce::Vector3D<float> Position() const { return position_; }
    juce::Vector3D<float> Target() const { return position_ + Forward(); }

private:
    void mouseDown(const juce::MouseEvent& event) override;

    juce::Vector3D<float> Forward() const;
    juce::Vector3D<float> FlatForward() const;

    juce::Component& viewport_;

    // Angled slightly down by default so a scene sitting near the origin
    // (like the seeded demo entity, at ground level) is actually in view
    // on launch -- pitch 0 from this height looks dead level, past it.
    juce::Vector3D<float> position_{ 0.0f, 1.6f, 5.0f };
    float yaw_ = 0.0f;     // radians; 0 looks down -Z.
    float pitch_ = -0.25f;
    juce::Point<float> lastMouseScreenPos_;
    bool wasLookingLastFrame_ = false;

    std::atomic<bool> isLooking_{ false };
    std::atomic<bool> firstPersonMode_{ false };
    std::atomic<float> speedMultiplier_{ 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeCamera)
};

} // namespace ce
