#pragma once

#include <functional>

#include <JuceHeader.h>

#include "Project/StarterGameTemplates.h"

namespace ce::views {

// File > New > Game's picker: choose a starter template, name the game,
// Create/Cancel. Modeled directly on creation::ui::SuiteAiAccountDialog's
// pattern (shared/UI) -- an ordinary Component hosted in its own
// DialogWindow, completion via callbacks rather than a modal int result.
class NewGameDialog final : public juce::Component {
public:
    explicit NewGameDialog(std::vector<project::StarterGameTemplate> templates);

    // Fired once: onCreate with the chosen template and trimmed name (never
    // empty -- defaults to the template's own display name), or
    // onCancelled. Never both.
    std::function<void(const juce::String& gameName, const project::StarterGameTemplate& chosenTemplate)> onCreate;
    std::function<void()> onCancelled;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void SelectTemplate(int index);

    std::vector<project::StarterGameTemplate> templates_;
    int selectedIndex_ = 0;

    juce::Label titleLabel_{ {}, "Starter Scene" };
    juce::ListBox templateList_{ "StarterTemplates" };
    class TemplateListModel;
    std::unique_ptr<TemplateListModel> listModel_;

    juce::Label descriptionLabel_;
    juce::Label nameLabel_{ {}, "Game Name" };
    juce::TextEditor nameEditor_;
    juce::TextButton createButton_{ "Create" };
    juce::TextButton cancelButton_{ "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewGameDialog)
};

// Shows the dialog in its own DialogWindow. onComplete fires with
// (true, gameName, chosenTemplate) on Create, or (false, {}, {}) on
// Cancel/close.
void showNewGameDialog(std::function<void(bool created, juce::String gameName, project::StarterGameTemplate chosenTemplate)> onComplete);

} // namespace ce::views
