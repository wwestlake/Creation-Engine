#include "Views/LogPanel.h"

namespace ce::views {

namespace {
constexpr int kTimestampColumnWidth = 150;
constexpr int kLevelColumnWidth = 56;
constexpr int kCategoryColumnWidth = 90;
constexpr int kRowHeight = 20;
} // namespace

LogPanel::LogPanel() {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    filterLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(filterLabel_);

    filterBox_.setTextToShowWhenEmpty("category or message contains...", juce::Colours::grey);
    filterBox_.onTextChange = [this] { RefreshFilteredEntries(); };
    addAndMakeVisible(filterBox_);

    clearButton_.onClick = [this] {
        diagnostics::EngineLog::Instance().Clear();
        RefreshFilteredEntries();
    };
    addAndMakeVisible(clearButton_);

    listBox_.setModel(this);
    listBox_.setRowHeight(kRowHeight);
    listBox_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff10141a));
    addAndMakeVisible(listBox_);

    diagnostics::EngineLog::Instance().onNewEntry = [this] { RefreshFilteredEntries(); };
    RefreshFilteredEntries();
}

LogPanel::~LogPanel() {
    // Only ONE LogPanel is ever expected to exist (a single dockable Log
    // window, same convention as every other single-instance panel in
    // this app) -- clearing the callback here just avoids a dangling
    // `this` if that ever changes without this destructor being noticed.
    diagnostics::EngineLog::Instance().onNewEntry = nullptr;
}

void LogPanel::RefreshFilteredEntries() {
    const auto snapshot = diagnostics::EngineLog::Instance().Snapshot();
    const auto filterText = filterBox_.getText().trim();

    if (filterText.isEmpty()) {
        filtered_ = snapshot;
    } else {
        filtered_.clear();
        for (const auto& entry : snapshot) {
            if (entry.category.containsIgnoreCase(filterText) || entry.message.containsIgnoreCase(filterText)) {
                filtered_.push_back(entry);
            }
        }
    }

    listBox_.updateContent();
    // Newest entry is last (matches EngineLog::Snapshot's own "newest
    // last" order) -- scrolling to it on every refresh gives a live
    // console-tail feel, same as every real log viewer.
    if (!filtered_.empty()) listBox_.scrollToEnsureRowIsOnscreen(static_cast<int>(filtered_.size()) - 1);
}

int LogPanel::getNumRows() {
    return static_cast<int>(filtered_.size());
}

void LogPanel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || static_cast<std::size_t>(rowNumber) >= filtered_.size()) return;
    const auto& entry = filtered_[static_cast<std::size_t>(rowNumber)];

    g.fillAll(rowIsSelected ? juce::Colour(0xff2a3540) : (rowNumber % 2 == 0 ? juce::Colour(0xff10141a) : juce::Colour(0xff141920)));

    const auto levelColour = diagnostics::ColourFor(entry.level);
    auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(4, 1);

    g.setColour(juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    auto timestampArea = bounds.removeFromLeft(kTimestampColumnWidth);
    g.drawText(entry.timestamp.formatted("%Y-%m-%d %H:%M:%S"), timestampArea, juce::Justification::centredLeft);

    g.setColour(levelColour);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
    auto levelArea = bounds.removeFromLeft(kLevelColumnWidth);
    g.drawText(diagnostics::ToString(entry.level), levelArea, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff8ea0b7));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    auto categoryArea = bounds.removeFromLeft(kCategoryColumnWidth);
    g.drawText(entry.category, categoryArea, juce::Justification::centredLeft);

    g.setColour(entry.level == diagnostics::LogLevel::Error || entry.level == diagnostics::LogLevel::Critical
                    ? levelColour
                    : juce::Colours::white);
    g.drawText(entry.message, bounds, juce::Justification::centredLeft);
}

void LogPanel::resized() {
    auto area = getLocalBounds().reduced(12, 8);

    titleLabel_.setBounds(area.removeFromTop(22));
    area.removeFromTop(6);

    auto filterRow = area.removeFromTop(24);
    filterLabel_.setBounds(filterRow.removeFromLeft(44));
    clearButton_.setBounds(filterRow.removeFromRight(70));
    filterRow.removeFromRight(6);
    filterBox_.setBounds(filterRow);
    area.removeFromTop(6);

    listBox_.setBounds(area);
}

void LogPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10141a));
}

} // namespace ce::views
