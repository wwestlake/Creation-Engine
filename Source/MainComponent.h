#pragma once

#include <JuceHeader.h>

#include "engine/world.h"
#include "node_system/graph.h"
#include "Render/ViewportComponent.h"
#include "Views/LightPanel.h"
#include "Views/PlaceholderPanel.h"
#include "Views/TransportBar.h"
#include "Views/ViewModeBar.h"

// The editor and the runtime are the same executable in different modes
// (capabilities spec, section 1). The top-level shell (TransportBar +
// ViewModeBar) mirrors Creation Station's — same color scheme, same tab-
// row/transport-bar structure — with Creation Engine's own tabs (Scene/
// Materials/Assets/Server/Settings) and a Play/Pause/Stop transport for
// the simulation itself (spec 3.3's "play-in-viewport") rather than an
// audio transport. Only Scene has a real panel today; the rest are
// PlaceholderPanel stand-ins until their own milestones land.
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void SetActiveMode(ce::WorkspaceMode mode);
    void SetPlaying(bool playing);

    ce::engine::World world_;
    ce::node_system::Graph exampleGraph_ { "untitled" };
    bool isPlaying_ = false;

    ce::TransportBar transportBar_;
    ce::ViewModeBar viewModeBar_;
    ce::WorkspaceMode activeMode_ = ce::WorkspaceMode::Scene;

    // --- Scene mode content ---
    ce::ViewportComponent viewport_;
    juce::Label inspectorTitle_ { {}, "Inspector" };
    juce::Label tickLabel_;

    juce::Label roughnessLabel_ { {}, "Roughness" };
    juce::Slider roughnessSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label metallicLabel_ { {}, "Metallic" };
    juce::Slider metallicSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    ce::LightPanel lightPanel_;

    // --- Other modes: stand-ins until their milestones land ---
    ce::PlaceholderPanel materialsPanel_ { "Materials", "Node-based material editor - coming soon" };
    ce::PlaceholderPanel assetsPanel_ { "Assets", "Asset catalog / VFS browser - coming soon" };
    ce::PlaceholderPanel serverPanel_ { "Server", "Dedicated server operational view - coming soon" };
    ce::PlaceholderPanel settingsPanel_ { "Settings", "Application settings - coming soon" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
