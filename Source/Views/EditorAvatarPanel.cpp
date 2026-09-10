#include "Views/EditorAvatarPanel.h"

namespace ce::views
{
namespace
{
constexpr int kMaleId = 1;
constexpr int kFemaleId = 2;
}

EditorAvatarPanel::EditorAvatarPanel()
{
    title_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    title_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title_);
    explanation_.setColour(juce::Label::textColourId, juce::Colour(0xffaebdce));
    explanation_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(explanation_);
    avatarLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd9e5f2));
    addAndMakeVisible(avatarLabel_);

    avatarChoice_.addItem("Editor Main Male", kMaleId);
    avatarChoice_.addItem("Editor Main Female", kFemaleId);
    avatarChoice_.setSelectedId(kMaleId, juce::dontSendNotification);
    avatarChoice_.onChange = [this] {
        UpdateDescription();
        if (onSelectionChanged) onSelectionChanged(SelectedAvatar());
    };
    addAndMakeVisible(avatarChoice_);
    description_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    description_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(description_);
    scope_.setColour(juce::Label::textColourId, juce::Colour(0xff6f849a));
    scope_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(scope_);
    UpdateDescription();
}

void EditorAvatarPanel::SetSelectedAvatar(const juce::String& assetId)
{
    avatarChoice_.setSelectedId(assetId == "EditorMainFemale" ? kFemaleId : kMaleId, juce::dontSendNotification);
    UpdateDescription();
}

juce::String EditorAvatarPanel::SelectedAvatar() const
{
    return avatarChoice_.getSelectedId() == kFemaleId ? "EditorMainFemale" : "EditorMainMale";
}

void EditorAvatarPanel::UpdateDescription()
{
    description_.setText(SelectedAvatar() == "EditorMainFemale"
                             ? "Female editor avatar. 2.00 m tall, clothed MakeHuman Game Engine rig."
                             : "Male editor avatar. 1.75 m tall, clothed MakeHuman Game Engine rig.",
                         juce::dontSendNotification);
}

void EditorAvatarPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    title_.setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    explanation_.setBounds(area.removeFromTop(42));
    area.removeFromTop(12);
    avatarLabel_.setBounds(area.removeFromTop(22));
    avatarChoice_.setBounds(area.removeFromTop(28));
    area.removeFromTop(10);
    description_.setBounds(area.removeFromTop(38));
    area.removeFromTop(8);
    scope_.setBounds(area.removeFromTop(40));
}
} // namespace ce::views
