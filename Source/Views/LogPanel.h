#pragma once

#include <JuceHeader.h>

#include "Diagnostics/EngineLog.h"

namespace ce::views {

// The Log window: a live, filterable view over ce::diagnostics::EngineLog
// -- timestamp/level/category/message, level-coloured, newest at the
// bottom like a console. A juce::ListBox (not a scrolling Component tree
// of real child rows, unlike e.g. ContentBrowserPanel's asset rows) --
// EngineLog can hold up to 5000 entries and a ListBox virtualizes what it
// actually draws, which a real-child-per-row approach would not.
//
// Read-only view onto EngineLog -- this panel writes nothing back to it
// except Clear(). Durable storage (writing entries to the project's VFS)
// is a completely separate concern, handled by EngineLogVfsWriter; this
// class doesn't know that exists.
class LogPanel final : public juce::Component, private juce::ListBoxModel {
public:
    LogPanel();
    ~LogPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    void RefreshFilteredEntries();

    std::vector<diagnostics::LogEntry> filtered_;

    juce::Label titleLabel_{ {}, "Log" };
    juce::Label filterLabel_{ {}, "Filter" };
    juce::TextEditor filterBox_;
    juce::TextButton clearButton_{ "Clear" };
    juce::ListBox listBox_{ "LogList", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LogPanel)
};

} // namespace ce::views
