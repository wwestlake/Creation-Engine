#pragma once

#include <JuceHeader.h>

#include "Render/ViewportComponent.h"

namespace ce {

// Basic lighting tooling per the capabilities spec (section 3.3/4.4):
// edit the sun's intensity, and add/remove/edit point lights (position +
// intensity), all live against the running viewport. Talks to
// ViewportComponent through its copy-in/copy-out light API — never holds
// a light reference across a frame, since renderOpenGL() runs on its own
// thread (see ViewportComponent.h for why).
class LightPanel final : public juce::Component {
public:
    explicit LightPanel(ViewportComponent& viewport);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Total height this panel needs for its current light count, so
    // MainComponent can size the column around it.
    int PreferredHeight() const;

private:
    class PointLightRow final : public juce::Component {
    public:
        PointLightRow(ViewportComponent& viewport, int index, std::function<void()> onRemoved);

        void resized() override;
        void paint(juce::Graphics& g) override;

        static constexpr int kHeight = 118;

    private:
        void PushToViewport();

        ViewportComponent& viewport_;
        int index_;
        std::function<void()> onRemoved_;

        juce::Label title_;
        juce::Label intensityLabel_{ {}, "Intensity" };
        juce::Slider intensitySlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Label positionLabel_{ {}, "Position (X / Y / Z)" };
        juce::Slider positionXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::Slider positionYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::Slider positionZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::TextButton removeButton_{ "Remove" };
    };

    void RebuildPointLightRows();

    ViewportComponent& viewport_;

    juce::Label sunTitle_{ {}, "Sun (Directional)" };
    juce::Label sunIntensityLabel_{ {}, "Intensity" };
    juce::Slider sunIntensitySlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label pointLightsTitle_{ {}, "Point Lights" };
    juce::OwnedArray<PointLightRow> pointLightRows_;
    juce::TextButton addPointLightButton_{ "+ Add Point Light" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightPanel)
};

} // namespace ce
