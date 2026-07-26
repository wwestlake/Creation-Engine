#include "Views/PlaceholderPanel.h"

namespace ce {

PlaceholderPanel::PlaceholderPanel(const juce::String& title, const juce::String& subtitle) {
    titleLabel_.setText(title, juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(24.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText(subtitle, juce::dontSendNotification);
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    subtitleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(subtitleLabel_);
}

void PlaceholderPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void PlaceholderPanel::resized() {
    auto area = getLocalBounds();
    const auto centreY = area.getCentreY();
    titleLabel_.setBounds(area.getX(), centreY - 30, area.getWidth(), 32);
    subtitleLabel_.setBounds(area.getX(), centreY + 4, area.getWidth(), 24);
}

} // namespace ce
