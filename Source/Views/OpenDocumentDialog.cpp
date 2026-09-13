#include "Views/OpenDocumentDialog.h"

namespace ce::views {
namespace {

class OpenDocumentDialog final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    OpenDocumentDialog(juce::String prompt, std::vector<OpenDocumentChoice> choices)
        : choices_(std::move(choices)), listBox_("Documents", this)
    {
        promptLabel_.setText(prompt, juce::dontSendNotification);
        promptLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffdce6f5));
        addAndMakeVisible(promptLabel_);

        listBox_.setRowHeight(42);
        listBox_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1a2230));
        listBox_.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a3a50));
        addAndMakeVisible(listBox_);

        openButton_.onClick = [this] { OpenSelected(); };
        addAndMakeVisible(openButton_);
        cancelButton_.onClick = [this] { if (onCancelled) onCancelled(); };
        addAndMakeVisible(cancelButton_);

        if (!choices_.empty()) listBox_.selectRow(0);
        setSize(480, 360);
    }

    std::function<void(juce::String)> onOpen;
    std::function<void()> onCancelled;

    void resized() override
    {
        auto area = getLocalBounds().reduced(16, 14);
        promptLabel_.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        auto buttons = area.removeFromBottom(32);
        listBox_.setBounds(area);
        cancelButton_.setBounds(buttons.removeFromRight(90));
        buttons.removeFromRight(8);
        openButton_.setBounds(buttons.removeFromRight(90));
    }

private:
    int getNumRows() override { return static_cast<int>(choices_.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (!juce::isPositiveAndBelow(row, static_cast<int>(choices_.size()))) return;
        const auto& choice = choices_[static_cast<std::size_t>(row)];
        g.fillAll(selected ? juce::Colour(0xff3a6ea8) : juce::Colour(0xff1a2230));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        g.drawText(choice.name, 8, 3, width - 16, 19, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff9fb1c7));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(choice.detail, 8, 21, width - 16, height - 22, juce::Justification::centredLeft);
    }

    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { OpenSelected(); }

    void OpenSelected()
    {
        const auto row = listBox_.getSelectedRow();
        if (!juce::isPositiveAndBelow(row, static_cast<int>(choices_.size()))) return;
        if (onOpen) onOpen(choices_[static_cast<std::size_t>(row)].id);
    }

    std::vector<OpenDocumentChoice> choices_;
    juce::Label promptLabel_;
    juce::ListBox listBox_;
    juce::TextButton openButton_{ "Open" };
    juce::TextButton cancelButton_{ "Cancel" };
};

} // namespace

void showOpenDocumentDialog(juce::String title,
                            juce::String prompt,
                            std::vector<OpenDocumentChoice> choices,
                            std::function<void(juce::String selectedId)> onOpen)
{
    auto* content = new OpenDocumentDialog(prompt, std::move(choices));
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = title;
    options.dialogBackgroundColour = juce::Colour(0xff182131);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.useBottomRightCornerResizer = true;

    auto* window = options.launchAsync();
    auto safeWindow = juce::Component::SafePointer<juce::DialogWindow>(window);
    content->onOpen = [safeWindow, onOpen = std::move(onOpen)](juce::String id) {
        if (onOpen) onOpen(std::move(id));
        if (safeWindow != nullptr) safeWindow->exitModalState(0);
    };
    content->onCancelled = [safeWindow] {
        if (safeWindow != nullptr) safeWindow->exitModalState(0);
    };
}

} // namespace ce::views
