#pragma once

#include <JuceHeader.h>

#include "engine/core_components.h"

namespace ce::views {

// A small, standalone 9-slider Transform editor -- position/rotation
// (degrees)/scale -- mirroring TransformPanel's own layout and unit
// conversion, but operating on a plain in-memory engine::Transform&
// instead of a live registry entity. TransformPanel is tightly coupled to
// World + a selected entt::entity (Refresh()/PushToRegistry() both read
// and write straight through the registry); there's no live entity here,
// just one ObjectComponentEntry's meshLocalTransform/childLocalTransform
// field sitting in memory until the Object Definition Editor's own Save.
// Used inside a juce::CallOutBox popup by ObjectDefinitionEditorPanel's
// per-component "Transform..." button.
class TransformFieldEditor final : public juce::Component {
public:
    TransformFieldEditor();

    void SetValue(const engine::Transform& transform);
    [[nodiscard]] engine::Transform GetValue() const;

    // Fired on every slider change; GetValue() already reflects the new
    // value by the time this runs.
    std::function<void()> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kPreferredWidth = 280;
    static constexpr int kPreferredHeight = 132;

private:
    juce::Label positionLabel_{ {}, "Position (X / Y / Z)" };
    juce::Slider positionXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider positionYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider positionZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label rotationLabel_{ {}, "Rotation deg (X / Y / Z)" };
    juce::Slider rotationXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider rotationYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider rotationZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label scaleLabel_{ {}, "Scale (X / Y / Z)" };
    juce::Slider scaleXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider scaleYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider scaleZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransformFieldEditor)
};

} // namespace ce::views
