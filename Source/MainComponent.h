#pragma once

#include <JuceHeader.h>

#include "engine/world.h"
#include "node_system/graph.h"
#include "Render/ViewportComponent.h"

// The editor and the runtime are the same executable in different modes
// (capabilities spec, section 1) — this component is where that split
// will live: a mode switch, not a second application. Right now it always
// runs in "editor" mode: viewport + a placeholder inspector panel, both
// in the same window/process, ticking the same World the game itself
// will simulate against.
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

    ce::engine::World world_;
    ce::node_system::Graph exampleGraph_ { "untitled" };

    ce::ViewportComponent viewport_;
    juce::Label inspectorTitle_ { {}, "Inspector" };
    juce::Label tickLabel_;

    juce::Label roughnessLabel_ { {}, "Roughness" };
    juce::Slider roughnessSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label metallicLabel_ { {}, "Metallic" };
    juce::Slider metallicSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
