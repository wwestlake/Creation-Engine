#pragma once

#include <atomic>

#include <JuceHeader.h>

namespace ce {

// Fly camera: hold the right mouse button over the viewport to look
// around (mouse delta -> yaw/pitch) and use WASD to move, Q/E for
// down/up — the standard "hold right-click to fly" convention (Unity's
// Scene view, among others). WASD is only live while right-click is
// held, not globally, so it can't steal keystrokes from some other
// focused control elsewhere in the UI.
//
// Look input is a proper juce::MouseListener attached to the viewport
// component, using JUCE's "unbounded mouse movement" mode
// (MouseEvent::source.enableUnboundedMouseMovement) while dragging —
// that's JUCE's own built-in mechanism for exactly this (FPS-style
// camera drag): it hides the cursor and keeps delivering relative deltas
// past the edge of the screen instead of clamping/losing them, which is
// what a first pass at this (polling juce::Desktop::getMousePosition())
// got wrong — dragging past the window edge silently stopped working,
// since a real absolute cursor position can't go past the screen edge.
// WASD movement is still read by polling (juce::KeyPress::
// isKeyCurrentlyDown) each frame from renderOpenGL() — no equivalent
// off-screen problem for keys, so no reason to route that through events.
//
// yaw_/pitch_/isLooking_ are set from the message thread (mouse events)
// and read every frame from the render thread — atomic for that reason,
// same rule as speedMultiplier_ (set from mouseWheelMove). position_ is
// only ever touched from the render thread's Update(), so it doesn't
// need the same treatment.
class FreeCamera final : private juce::MouseListener {
public:
    explicit FreeCamera(juce::Component& viewport);
    ~FreeCamera() override;

    // Call once per frame from the render thread — advances position_
    // based on WASD/Q/E while a look-drag is active.
    void Update(float deltaSeconds);
    void AdjustSpeed(float wheelDeltaY);

    juce::Vector3D<float> Position() const { return position_; }
    juce::Vector3D<float> Target() const { return position_ + Forward(); }

private:
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    juce::Vector3D<float> Forward() const;
    juce::Vector3D<float> FlatForward() const;

    juce::Component& viewport_;

    juce::Vector3D<float> position_{ 0.0f, 1.6f, 5.0f };
    std::atomic<float> yaw_{ 0.0f };   // radians; 0 looks down -Z.
    std::atomic<float> pitch_{ 0.0f };
    std::atomic<bool> isLooking_{ false };

    juce::Point<float> lastDragScreenPos_;
    std::atomic<float> speedMultiplier_{ 1.0f };
};

} // namespace ce
