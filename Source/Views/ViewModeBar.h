#pragma once

#include <array>

#include <JuceHeader.h>

namespace ce {

enum class WorkspaceMode { Scene, Logic, Materials, Assets, Server, Settings };

juce::String WorkspaceModeName(WorkspaceMode mode);

// Mode tab row, same visual pattern as Creation Station's ViewModeBar:
// title, a row of toggle buttons (one per mode), and an active-mode
// indicator drawn on the right. No "Pop Out" (undocking a panel into its
// own window) yet — a reasonable follow-up, not built here.
class ViewModeBar final : public juce::Component {
public:
    ViewModeBar();

    std::function<void(WorkspaceMode)> onModeSelected;

    void SetActiveMode(WorkspaceMode mode);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kModeCount = 6;

    WorkspaceMode activeMode_ = WorkspaceMode::Scene;
    juce::Label titleLabel_{ {}, "Creative Modes" };
    std::array<juce::TextButton, kModeCount> modeButtons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewModeBar)
};

} // namespace ce
