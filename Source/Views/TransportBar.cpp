#include "Views/TransportBar.h"

#include "Branding.h"

namespace ce {

TransportBar::TransportBar() {
    logoImage_ = branding::CreateLogoImage(72);

    titleLabel_.setFont(juce::Font(juce::FontOptions(28.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    statusLabel_.setText("Editing", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(statusLabel_);

    for (auto* button : { &playButton_, &pauseButton_, &stopButton_ }) {
        button->setLookAndFeel(&transportLookAndFeel_);
        button->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    playButton_.setTooltip("Play");
    pauseButton_.setTooltip("Pause");
    stopButton_.setTooltip("Stop");

    playButton_.onClick = [this] {
        if (onPlay) {
            onPlay();
        }
    };
    pauseButton_.onClick = [this] {
        if (onPause) {
            onPause();
        }
    };
    stopButton_.onClick = [this] {
        if (onStop) {
            onStop();
        }
    };
}

TransportBar::~TransportBar() {
    for (auto* button : { &playButton_, &pauseButton_, &stopButton_ }) {
        button->setLookAndFeel(nullptr);
    }
}

void TransportBar::SetPlaying(bool isPlaying) {
    playButton_.setToggleState(isPlaying, juce::dontSendNotification);
}

void TransportBar::SetStatusText(const juce::String& text) {
    statusLabel_.setText(text, juce::dontSendNotification);
}

void TransportBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0f1115));

    g.setColour(juce::Colour(0xff242a36));
    g.drawLine(0.0f, static_cast<float>(getHeight()) - 1.0f, static_cast<float>(getWidth()),
               static_cast<float>(getHeight()) - 1.0f, 1.0f);

    if (logoImage_.isValid()) {
        const auto logoSize = 44;
        g.drawImageWithin(logoImage_, getWidth() - logoSize - 14, 9, logoSize, logoSize,
                           juce::RectanglePlacement::centred, false);
    }
}

void TransportBar::resized() {
    auto area = getLocalBounds().reduced(18, 10);
    area.removeFromRight(58); // logo

    auto topRow = area.removeFromTop(30);
    titleLabel_.setBounds(topRow.removeFromLeft(280));
    statusLabel_.setBounds(topRow);

    auto transportRow = area;
    constexpr int buttonWidth = 56;
    playButton_.setBounds(transportRow.removeFromLeft(buttonWidth));
    transportRow.removeFromLeft(6);
    pauseButton_.setBounds(transportRow.removeFromLeft(buttonWidth));
    transportRow.removeFromLeft(6);
    stopButton_.setBounds(transportRow.removeFromLeft(buttonWidth));
}

} // namespace ce
