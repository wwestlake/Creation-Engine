#include "Views/MaterialsPanel.h"

#include <mutex>

#include "Scene/Components.h"

namespace ce {

namespace {
constexpr int kRowGap = 4;
constexpr int kLabelHeight = 16;
constexpr int kSliderHeight = 22;
} // namespace

MaterialsPanel::MaterialsPanel(engine::World& world) : world_(world) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noSelectionLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noSelectionLabel_);

    albedoLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(albedoLabel_);
    for (auto* slider : { &albedoRSlider_, &albedoGSlider_, &albedoBSlider_ }) {
        slider->setRange(0.0, 1.0);
        slider->onValueChange = [this] { PushToRegistry(); };
        addAndMakeVisible(slider);
    }

    metallicLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(metallicLabel_);
    metallicSlider_.setRange(0.0, 1.0);
    metallicSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(metallicSlider_);

    roughnessLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(roughnessLabel_);
    roughnessSlider_.setRange(0.05, 1.0);
    roughnessSlider_.onValueChange = [this] { PushToRegistry(); };
    addAndMakeVisible(roughnessSlider_);

    SetEditorsVisible(false);
}

void MaterialsPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
    Refresh();
}

bool MaterialsPanel::AnySliderBeingDragged() const {
    for (const auto* slider :
         { &albedoRSlider_, &albedoGSlider_, &albedoBSlider_, &metallicSlider_, &roughnessSlider_ }) {
        if (slider->getThumbBeingDragged() >= 0) {
            return true;
        }
    }
    return false;
}

void MaterialsPanel::Refresh() {
    if (selectedEntity_ == entt::null) {
        SetEditorsVisible(false);
        return;
    }

    if (AnySliderBeingDragged()) {
        return;
    }

    std::shared_ptr<Material> material;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(selectedEntity_) && registry.all_of<scene::MeshRenderer>(selectedEntity_)) {
            material = registry.get<scene::MeshRenderer>(selectedEntity_).material;
            if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(selectedEntity_)) {
                locked_ = sceneFlags->locked;
            } else {
                locked_ = false;
            }
        }
    }

    if (material == nullptr) {
        // Selection points at a folder/entity with no MeshRenderer (or no
        // material assigned), or a since-deleted entity -- either way,
        // nothing here to edit right now.
        selectedEntity_ = entt::null;
        SetEditorsVisible(false);
        return;
    }

    albedoRSlider_.setValue(material->albedo.x, juce::dontSendNotification);
    albedoGSlider_.setValue(material->albedo.y, juce::dontSendNotification);
    albedoBSlider_.setValue(material->albedo.z, juce::dontSendNotification);
    metallicSlider_.setValue(material->metallic, juce::dontSendNotification);
    roughnessSlider_.setValue(material->roughness, juce::dontSendNotification);

    SetEditorsVisible(true);

    const bool enabled = !locked_;
    for (auto* slider : { &albedoRSlider_, &albedoGSlider_, &albedoBSlider_, &metallicSlider_, &roughnessSlider_ }) {
        slider->setEnabled(enabled);
    }
}

void MaterialsPanel::PushToRegistry() {
    if (selectedEntity_ == entt::null || locked_) {
        return;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selectedEntity_) || !registry.all_of<scene::MeshRenderer>(selectedEntity_)) {
        return;
    }

    auto& material = registry.get<scene::MeshRenderer>(selectedEntity_).material;
    if (material == nullptr) {
        return;
    }

    material->albedo = { static_cast<float>(albedoRSlider_.getValue()), static_cast<float>(albedoGSlider_.getValue()),
                          static_cast<float>(albedoBSlider_.getValue()) };
    material->metallic = static_cast<float>(metallicSlider_.getValue());
    material->roughness = static_cast<float>(roughnessSlider_.getValue());
}

void MaterialsPanel::SetEditorsVisible(bool visible) {
    noSelectionLabel_.setVisible(!visible);
    for (juce::Component* component : std::initializer_list<juce::Component*>{
             &albedoLabel_, &albedoRSlider_, &albedoGSlider_, &albedoBSlider_, &metallicLabel_, &metallicSlider_,
             &roughnessLabel_, &roughnessSlider_ }) {
        component->setVisible(visible);
    }
}

void MaterialsPanel::paint(juce::Graphics&) {}

void MaterialsPanel::resized() {
    auto bounds = getLocalBounds();

    titleLabel_.setBounds(bounds.removeFromTop(20));
    noSelectionLabel_.setBounds(bounds.removeFromTop(kLabelHeight));

    albedoLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    auto albedoRow = bounds.removeFromTop(kSliderHeight);
    const int third = albedoRow.getWidth() / 3;
    albedoRSlider_.setBounds(albedoRow.removeFromLeft(third));
    albedoGSlider_.setBounds(albedoRow.removeFromLeft(third));
    albedoBSlider_.setBounds(albedoRow);
    bounds.removeFromTop(kRowGap);

    metallicLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    metallicSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
    bounds.removeFromTop(kRowGap);

    roughnessLabel_.setBounds(bounds.removeFromTop(kLabelHeight));
    roughnessSlider_.setBounds(bounds.removeFromTop(kSliderHeight));
}

} // namespace ce
