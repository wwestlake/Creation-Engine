#pragma once

#include <atomic>

#include <JuceHeader.h>

namespace ce {

// Fly camera: hold the right mouse button over the viewport to look
// around (mouse delta -> yaw/pitch) and use WASD to move, Q/E for
// down/up — the standard "hold right-click to fly" convention (Unity's
// Scene view, among others). WASD is only live while right-click is
// held, not globally, so it can't steal keystrokes from some other
// focused text field elsewhere in the UI.
//
// Reads input by polling (juce::KeyPress::isKeyCurrentlyDown,
// juce::Desktop::getMousePosition, juce::ModifierKeys::
// getCurrentModifiers) rather than JUCE mouse/key event callbacks, so
// Update() can be called directly from renderOpenGL() every frame — all
// of this state stays render-thread-only, avoiding a repeat of the
// cross-thread hazards fixed elsewhere in this render layer (M5, SC1).
// The one exception is AdjustSpeed(), called from the message thread's
// mouseWheelMove — speedMultiplier_ is atomic for exactly that reason.
class FreeCamera final {
public:
    // viewportScreenBounds: only look/move while the mouse is within
    // this rectangle (screen coordinates) and the right button is held.
    void Update(float deltaSeconds, juce::Rectangle<int> viewportScreenBounds);
    void AdjustSpeed(float wheelDeltaY);

    juce::Vector3D<float> Position() const { return position_; }
    juce::Vector3D<float> Target() const { return position_ + Forward(); }

private:
    juce::Vector3D<float> Forward() const;
    juce::Vector3D<float> FlatForward() const;

    juce::Vector3D<float> position_{ 0.0f, 1.6f, 5.0f };
    float yaw_ = 0.0f;   // radians; 0 looks down -Z.
    float pitch_ = 0.0f;

    juce::Point<float> lastMousePos_;
    bool hasLastMousePos_ = false;

    std::atomic<float> speedMultiplier_{ 1.0f };
};

} // namespace ce
