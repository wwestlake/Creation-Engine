#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ce::views
{
class EditorAvatarPanel final : public juce::Component
{
public:
    using SelectionChanged = std::function<void(const juce::String&)>;

    EditorAvatarPanel();
    void SetSelectedAvatar(const juce::String& assetId);
    [[nodiscard]] juce::String SelectedAvatar() const;

    SelectionChanged onSelectionChanged;
    void resized() override;

private:
    void UpdateDescription();

    juce::Label title_ { {}, "Editor Avatar" };
    juce::Label explanation_ { {}, "Choose the character used for walking and riding the editor cart." };
    juce::Label avatarLabel_ { {}, "Character" };
    juce::ComboBox avatarChoice_;
    juce::Label description_;
    juce::Label scope_ { {}, "Engine application asset. This choice is not a Game Player Slot." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorAvatarPanel)
};
} // namespace ce::views
