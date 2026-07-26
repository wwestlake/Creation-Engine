#pragma once

#include <JuceHeader.h>

namespace ce {

// Stand-in content for a workspace mode that doesn't have a real panel
// yet (Materials/Assets/Server/Settings) — same background/text colors
// as the rest of the shell, so switching to an unbuilt tab looks
// intentional rather than broken.
class PlaceholderPanel final : public juce::Component {
public:
    PlaceholderPanel(const juce::String& title, const juce::String& subtitle);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
};

} // namespace ce
