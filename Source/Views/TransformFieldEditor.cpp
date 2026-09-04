#include "Views/TransformFieldEditor.h"

namespace ce::views {

namespace {
constexpr int kRowGap = 4;
constexpr int kLabelHeight = 16;
constexpr int kSliderHeight = 22;
} // namespace

TransformFieldEditor::TransformFieldEditor() {
    for (auto* label : { &positionLabel_, &rotationLabel_, &scaleLabel_ }) {
        label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    }

    for (auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_ }) {
        slider->setRange(-1000.0, 1000.0);
        slider->onValueChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible(slider);
    }
    for (auto* slider : { &rotationXSlider_, &rotationYSlider_, &rotationZSlider_ }) {
        slider->setRange(-180.0, 180.0);
        slider->onValueChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible(slider);
    }
    for (auto* slider : { &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        slider->setRange(0.01, 10.0);
        slider->onValueChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible(slider);
    }

    setSize(kPreferredWidth, kPreferredHeight);
}

void TransformFieldEditor::SetValue(const engine::Transform& transform) {
    positionXSlider_.setValue(transform.position.x, juce::dontSendNotification);
    positionYSlider_.setValue(transform.position.y, juce::dontSendNotification);
    positionZSlider_.setValue(transform.position.z, juce::dontSendNotification);

    rotationXSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.x), juce::dontSendNotification);
    rotationYSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.y), juce::dontSendNotification);
    rotationZSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.z), juce::dontSendNotification);

    scaleXSlider_.setValue(transform.scale.x, juce::dontSendNotification);
    scaleYSlider_.setValue(transform.scale.y, juce::dontSendNotification);
    scaleZSlider_.setValue(transform.scale.z, juce::dontSendNotification);
}

engine::Transform TransformFieldEditor::GetValue() const {
    engine::Transform transform;
    transform.position = { static_cast<float>(positionXSlider_.getValue()), static_cast<float>(positionYSlider_.getValue()),
                            static_cast<float>(positionZSlider_.getValue()) };
    transform.eulerRotationRadians = { juce::degreesToRadians(static_cast<float>(rotationXSlider_.getValue())),
                                        juce::degreesToRadians(static_cast<float>(rotationYSlider_.getValue())),
                                        juce::degreesToRadians(static_cast<float>(rotationZSlider_.getValue())) };
    transform.scale = { static_cast<float>(scaleXSlider_.getValue()), static_cast<float>(scaleYSlider_.getValue()),
                         static_cast<float>(scaleZSlider_.getValue()) };
    return transform;
}

void TransformFieldEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10141a));
}

void TransformFieldEditor::resized() {
    auto bounds = getLocalBounds().reduced(8);

    positionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto positionRow = bounds.removeFromTop(kSliderHeight);
    int third = positionRow.getWidth() / 3;
    positionXSlider_.setBounds(positionRow.removeFromLeft(third));
    positionYSlider_.setBounds(positionRow.removeFromLeft(third));
    positionZSlider_.setBounds(positionRow);
    bounds.removeFromTop(kRowGap);

    rotationLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto rotationRow = bounds.removeFromTop(kSliderHeight);
    third = rotationRow.getWidth() / 3;
    rotationXSlider_.setBounds(rotationRow.removeFromLeft(third));
    rotationYSlider_.setBounds(rotationRow.removeFromLeft(third));
    rotationZSlider_.setBounds(rotationRow);
    bounds.removeFromTop(kRowGap);

    scaleLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto scaleRow = bounds.removeFromTop(kSliderHeight);
    third = scaleRow.getWidth() / 3;
    scaleXSlider_.setBounds(scaleRow.removeFromLeft(third));
    scaleYSlider_.setBounds(scaleRow.removeFromLeft(third));
    scaleZSlider_.setBounds(scaleRow);
}

} // namespace ce::views
