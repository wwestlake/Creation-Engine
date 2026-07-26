#include "Views/TransformPanel.h"

#include <mutex>

#include "Scene/Components.h"

namespace ce {

namespace {
constexpr int kRowGap = 4;
constexpr int kLabelHeight = 16;
constexpr int kSliderHeight = 22;
} // namespace

TransformPanel::TransformPanel(engine::World& world) : world_(world) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noSelectionLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noSelectionLabel_);

    for (auto* label : { &positionLabel_, &rotationLabel_, &scaleLabel_ }) {
        label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    }

    for (auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_ }) {
        slider->setRange(-20.0, 20.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }
    for (auto* slider : { &rotationXSlider_, &rotationYSlider_, &rotationZSlider_ }) {
        slider->setRange(-180.0, 180.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }
    for (auto* slider : { &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        slider->setRange(0.01, 10.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }

    SetEditorsVisible(false);
}

void TransformPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
    Refresh();
}

bool TransformPanel::AnySliderBeingDragged() const {
    for (const auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_, &rotationXSlider_,
                                 &rotationYSlider_, &rotationZSlider_, &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        if (slider->getThumbBeingDragged() >= 0) {
            return true;
        }
    }
    return false;
}

void TransformPanel::Refresh() {
    if (selectedEntity_ == entt::null) {
        SetEditorsVisible(false);
        return;
    }

    if (AnySliderBeingDragged()) {
        return;
    }

    scene::Transform transform;
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(selectedEntity_) && registry.all_of<scene::Transform>(selectedEntity_)) {
            transform = registry.get<scene::Transform>(selectedEntity_);
            valid = true;
            if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(selectedEntity_)) {
                locked_ = sceneFlags->locked;
            } else {
                locked_ = false;
            }
        }
    }

    if (!valid) {
        // Selection points at a folder (no Transform) or a since-deleted
        // entity -- either way, nothing here to edit right now.
        selectedEntity_ = entt::null;
        SetEditorsVisible(false);
        return;
    }

    positionXSlider_.setValue(transform.position.x, juce::dontSendNotification);
    positionYSlider_.setValue(transform.position.y, juce::dontSendNotification);
    positionZSlider_.setValue(transform.position.z, juce::dontSendNotification);

    rotationXSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.x), juce::dontSendNotification);
    rotationYSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.y), juce::dontSendNotification);
    rotationZSlider_.setValue(juce::radiansToDegrees(transform.eulerRotationRadians.z), juce::dontSendNotification);

    scaleXSlider_.setValue(transform.scale.x, juce::dontSendNotification);
    scaleYSlider_.setValue(transform.scale.y, juce::dontSendNotification);
    scaleZSlider_.setValue(transform.scale.z, juce::dontSendNotification);

    SetEditorsVisible(true);

    const bool enabled = !locked_;
    for (auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_, &rotationXSlider_, &rotationYSlider_,
                           &rotationZSlider_, &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        slider->setEnabled(enabled);
    }
}

void TransformPanel::PushToRegistry() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_) || !registry.all_of<scene::Transform>(selectedEntity_)) {
        return;
    }

    auto& transform = registry.get<scene::Transform>(selectedEntity_);
    transform.position = { static_cast<float>(positionXSlider_.getValue()), static_cast<float>(positionYSlider_.getValue()),
                            static_cast<float>(positionZSlider_.getValue()) };
    transform.eulerRotationRadians = { juce::degreesToRadians(static_cast<float>(rotationXSlider_.getValue())),
                                        juce::degreesToRadians(static_cast<float>(rotationYSlider_.getValue())),
                                        juce::degreesToRadians(static_cast<float>(rotationZSlider_.getValue())) };
    transform.scale = { static_cast<float>(scaleXSlider_.getValue()), static_cast<float>(scaleYSlider_.getValue()),
                         static_cast<float>(scaleZSlider_.getValue()) };
}

void TransformPanel::SetEditorsVisible(bool visible) {
    noSelectionLabel_.setVisible(!visible);
    for (juce::Component* component : std::initializer_list<juce::Component*>{
             &positionLabel_, &positionXSlider_, &positionYSlider_, &positionZSlider_, &rotationLabel_, &rotationXSlider_,
             &rotationYSlider_, &rotationZSlider_, &scaleLabel_, &scaleXSlider_, &scaleYSlider_, &scaleZSlider_ }) {
        component->setVisible(visible);
    }
}

void TransformPanel::paint(juce::Graphics&) {}

void TransformPanel::resized() {
    auto bounds = getLocalBounds();

    titleLabel_.setBounds(bounds.removeFromTop(20));
    noSelectionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));

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

} // namespace ce
