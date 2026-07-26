#pragma once

#include <JuceHeader.h>

namespace ce {

// Hand-drawn transport icons (triangle/bars/square), same visual
// language as Creation Station's TransportButtonLookAndFeel: a rounded
// pill background with an accent glow ring when toggled/active, cyan
// accent, no bitmap/font icon assets.
class TransportLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton,
                         bool isButtonDown) override;

private:
    static void DrawIcon(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& iconName);
};

} // namespace ce
