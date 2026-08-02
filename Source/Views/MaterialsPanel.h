#pragma once

#include <JuceHeader.h>

#include "engine/world.h"

namespace ce {

// Albedo/metallic/roughness editors for whichever entity is selected in
// HierarchyPanel -- same selection-driven pattern as TransformPanel
// (reads/writes against the same World the render thread iterates every
// frame, same RegistryMutex discipline, same "skip while dragging" guard
// against Refresh()'s 30Hz poll fighting an in-progress edit, same
// SceneFlags::locked disable). Replaces MainComponent's old viewport-
// global roughness/metallic sliders (ViewportComponent::SetRoughness/
// SetMetallic, now removed), which applied to every entity's Material at
// once and had no albedo control at all.
//
// Reads/writes the selected entity's scene::MeshRenderer::material
// fields DIRECTLY -- Material is a shared, non-owning asset reference
// (MeshRenderer's own comment: several entities can point at the same
// Mesh/Material without duplicating GPU resources), so editing it here
// edits every other entity currently sharing that Material too. That's
// an existing trade-off the editor already accepts for Mesh, not a new
// one introduced here; a "make unique"/instance-material action is a
// natural follow-up, not built.
//
// Distinct from MainComponent's `materialsPanel_` (the WorkspaceMode::
// Materials tab's future node-based material editor, still a
// PlaceholderPanel) -- this is the small per-entity PBR numeric editor
// living in Scene mode's inspector sidebar, next to LightPanel.
class MaterialsPanel final : public juce::Component {
public:
    explicit MaterialsPanel(engine::World& world);

    void SetSelectedEntity(entt::entity entity);

    // Re-reads the selected entity's Material and refreshes the
    // displayed values -- called every timer tick from MainComponent,
    // same as TransformPanel::Refresh(). Skips the read while a slider is
    // actively being dragged so a concurrent Refresh() can't fight the
    // user's own in-progress edit.
    void Refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kPreferredHeight = 218;

private:
    void PushToRegistry();
    bool AnySliderBeingDragged() const;
    void SetEditorsVisible(bool visible);

    engine::World& world_;
    entt::entity selectedEntity_ = entt::null;
    bool locked_ = false;

    juce::Label titleLabel_{ {}, "Material" };
    juce::Label noSelectionLabel_{ {}, "No entity selected" };

    juce::Label albedoLabel_{ {}, "Albedo (R / G / B)" };
    juce::Slider albedoRSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider albedoGSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider albedoBSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label metallicLabel_{ {}, "Metallic" };
    juce::Slider metallicSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label roughnessLabel_{ {}, "Roughness" };
    juce::Slider roughnessSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaterialsPanel)
};

} // namespace ce
