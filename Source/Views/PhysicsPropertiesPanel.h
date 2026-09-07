#pragma once

#include <JuceHeader.h>

#include "engine/world.h"

namespace ce {

// Editor UI/Workflow Overhaul plan, Phase 2: direct editing of
// ce::physics::RigidBodyComponent/ColliderComponent for whichever entity is
// selected -- previously the ONLY way either component ever existed on an
// entity was a Pod's Schematic graph calling core.physics.setRigidBody/
// setColliderShape at on_start. Emplacing/editing them here instead just
// writes the same components a Pod would have -- PhysicsWorld::Advance()'s
// reconciliation pass creates the real Jolt body off them regardless of how
// they got set, so nothing about PhysicsWorld itself changes.
//
// Same selection-driven shape as TransformPanel/MaterialsPanel (skip-while-
// dragging against the 30Hz Refresh() poll, SceneFlags::locked disables
// editing) -- RigidBodyComponent/ColliderComponent are both optional, so
// this also has an explicit "no physics yet" state with an Add button,
// distinct from "locked" and from "no entity selected".
class PhysicsPropertiesPanel final : public juce::Component {
public:
    explicit PhysicsPropertiesPanel(engine::World& world);

    void SetSelectedEntity(entt::entity entity);
    void Refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

    int PreferredHeight() const;

private:
    void PushToRegistry();
    bool AnySliderBeingDragged() const;
    void SetNoSelectionState(bool noSelection);
    void SetHasPhysicsState(bool hasPhysics);
    void UpdateShapeFieldVisibility();
    void AddPhysics();
    void RemovePhysics();

    engine::World& world_;
    entt::entity selectedEntity_ = entt::null;
    bool locked_ = false;
    bool hasPhysics_ = false;

    juce::Label titleLabel_{ {}, "Physics" };
    juce::Label noSelectionLabel_{ {}, "No entity selected" };
    juce::TextButton addPhysicsButton_{ "Add Physics" };
    juce::TextButton removePhysicsButton_{ "Remove Physics" };

    juce::Label motionTypeLabel_{ {}, "Motion Type" };
    juce::ComboBox motionTypeCombo_;

    juce::Label massLabel_{ {}, "Mass" };
    juce::Slider massSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label frictionLabel_{ {}, "Friction" };
    juce::Slider frictionSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label restitutionLabel_{ {}, "Restitution" };
    juce::Slider restitutionSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label dampingLabel_{ {}, "Damping (Linear / Angular)" };
    juce::Slider linearDampingSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider angularDampingSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    juce::Label shapeLabel_{ {}, "Collider Shape" };
    juce::ComboBox shapeCombo_;
    juce::Label halfExtentLabel_{ {}, "Half Extent (X / Y / Z)" };
    juce::Slider halfExtentXSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider halfExtentYSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider halfExtentZSlider_{ juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label radiusLabel_{ {}, "Radius" };
    juce::Slider radiusSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label halfHeightLabel_{ {}, "Half Height" };
    juce::Slider halfHeightSlider_{ juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton isSensorToggle_{ "Sensor (trigger, no physical response)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhysicsPropertiesPanel)
};

} // namespace ce
