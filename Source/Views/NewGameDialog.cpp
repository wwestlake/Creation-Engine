#include "Views/NewGameDialog.h"

#include <algorithm>

namespace ce::views {

class NewGameDialog::TemplateListModel final : public juce::ListBoxModel {
public:
    explicit TemplateListModel(NewGameDialog& owner) : owner_(owner) {}

    int getNumRows() override { return static_cast<int>(owner_.templates_.size()); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        if (rowNumber < 0 || static_cast<std::size_t>(rowNumber) >= owner_.templates_.size()) return;
        g.fillAll(rowIsSelected ? juce::Colour(0xff3a6ea8) : juce::Colour(0xff1a2230));
        g.setColour(juce::Colours::white);
        g.drawText(owner_.templates_[static_cast<std::size_t>(rowNumber)].displayName, 8, 0, width - 16, height,
                    juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int lastRowSelected) override { owner_.SelectTemplate(lastRowSelected); }

private:
    NewGameDialog& owner_;
};

NewGameDialog::NewGameDialog(std::vector<project::StarterGameTemplate> templates) : templates_(std::move(templates)) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    listModel_ = std::make_unique<TemplateListModel>(*this);
    templateList_.setModel(listModel_.get());
    templateList_.setRowHeight(24);
    templateList_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1a2230));
    templateList_.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a3a50));
    addAndMakeVisible(templateList_);

    descriptionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    descriptionLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(descriptionLabel_);

    nameLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffdce6f5));
    addAndMakeVisible(nameLabel_);
    nameEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1a2230));
    nameEditor_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a3a50));
    nameEditor_.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(nameEditor_);

    createButton_.onClick = [this] {
        if (selectedIndex_ < 0 || static_cast<std::size_t>(selectedIndex_) >= templates_.size()) return;
        const auto& chosen = templates_[static_cast<std::size_t>(selectedIndex_)];
        const auto name = nameEditor_.getText().trim();
        if (onCreate) onCreate(name.isNotEmpty() ? name : chosen.displayName, chosen);
    };
    addAndMakeVisible(createButton_);

    cancelButton_.onClick = [this] { if (onCancelled) onCancelled(); };
    addAndMakeVisible(cancelButton_);

    if (!templates_.empty()) {
        templateList_.selectRow(0);
        SelectTemplate(0);
    }

    setSize(460, 380);
}

void NewGameDialog::SelectTemplate(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= templates_.size()) return;
    selectedIndex_ = index;
    const auto& chosen = templates_[static_cast<std::size_t>(index)];
    descriptionLabel_.setText(chosen.description, juce::dontSendNotification);
    // Only overwrite the name field if it still holds a previous template's
    // display name (or is empty) -- don't clobber something the user
    // actually typed themselves.
    const auto currentText = nameEditor_.getText().trim();
    const bool looksUntouched = currentText.isEmpty() ||
        std::any_of(templates_.begin(), templates_.end(),
                    [&](const auto& t) { return t.displayName == currentText; });
    if (looksUntouched) nameEditor_.setText(chosen.displayName, juce::dontSendNotification);
}

void NewGameDialog::resized() {
    auto area = getLocalBounds().reduced(16, 12);

    titleLabel_.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    templateList_.setBounds(area.removeFromTop(180));
    area.removeFromTop(8);

    descriptionLabel_.setBounds(area.removeFromTop(44));
    area.removeFromTop(8);

    nameLabel_.setBounds(area.removeFromTop(18));
    nameEditor_.setBounds(area.removeFromTop(26));
    area.removeFromTop(16);

    auto buttonsRow = area.removeFromTop(30);
    cancelButton_.setBounds(buttonsRow.removeFromRight(90));
    buttonsRow.removeFromRight(8);
    createButton_.setBounds(buttonsRow.removeFromRight(90));
}

void NewGameDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff182131));
}

void showNewGameDialog(std::function<void(bool created, juce::String gameName, project::StarterGameTemplate chosenTemplate)> onComplete) {
    auto* content = new NewGameDialog(project::GetStarterGameTemplates());

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = "New Game";
    options.dialogBackgroundColour = juce::Colour(0xff182131);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;

    auto* window = options.launchAsync();
    auto safeWindow = juce::Component::SafePointer<juce::DialogWindow>(window);

    content->onCreate = [safeWindow, onComplete](const juce::String& gameName, const project::StarterGameTemplate& chosenTemplate) {
        if (onComplete) onComplete(true, gameName, chosenTemplate);
        if (safeWindow != nullptr) safeWindow->exitModalState(0);
    };
    content->onCancelled = [safeWindow, onComplete] {
        if (onComplete) onComplete(false, {}, {});
        if (safeWindow != nullptr) safeWindow->exitModalState(0);
    };
}

} // namespace ce::views
