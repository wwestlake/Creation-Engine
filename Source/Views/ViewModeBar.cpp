#include "Views/ViewModeBar.h"

namespace ce {

juce::String WorkspaceModeName(WorkspaceMode mode) {
    switch (mode) {
        case WorkspaceMode::Scene:
            return "Scene";
        case WorkspaceMode::Materials:
            return "Materials";
        case WorkspaceMode::Assets:
            return "Assets";
        case WorkspaceMode::Server:
            return "Server";
        case WorkspaceMode::Settings:
            return "Settings";
    }
    return "Scene";
}

ViewModeBar::ViewModeBar() {
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    for (int i = 0; i < kModeCount; ++i) {
        const auto mode = static_cast<WorkspaceMode>(i);
        auto& button = modeButtons_[static_cast<std::size_t>(i)];
        button.setButtonText(WorkspaceModeName(mode));
        button.setClickingTogglesState(true);
        button.onClick = [this, mode] {
            SetActiveMode(mode);
            if (onModeSelected) {
                onModeSelected(mode);
            }
        };
        addAndMakeVisible(button);
    }

    SetActiveMode(WorkspaceMode::Scene);
}

void ViewModeBar::SetActiveMode(WorkspaceMode mode) {
    activeMode_ = mode;
    for (int i = 0; i < kModeCount; ++i) {
        modeButtons_[static_cast<std::size_t>(i)].setToggleState(static_cast<WorkspaceMode>(i) == activeMode_,
                                                                   juce::dontSendNotification);
    }
    repaint();
}

void ViewModeBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10141a));
    g.setColour(juce::Colour(0xff263140));
    g.drawLine(0.0f, static_cast<float>(getHeight()) - 1.0f, static_cast<float>(getWidth()),
               static_cast<float>(getHeight()) - 1.0f, 1.0f);

    g.setColour(juce::Colour(0xff8ea0b7));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(WorkspaceModeName(activeMode_) + " active", getLocalBounds().reduced(12, 0),
               juce::Justification::centredRight, true);
}

void ViewModeBar::resized() {
    auto area = getLocalBounds().reduced(14, 8);
    titleLabel_.setBounds(area.removeFromLeft(140));
    area.removeFromLeft(8);

    constexpr int buttonWidth = 96;
    for (auto& button : modeButtons_) {
        button.setBounds(area.removeFromLeft(buttonWidth));
    }
}

} // namespace ce
