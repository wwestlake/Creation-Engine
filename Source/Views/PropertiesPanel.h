#pragma once

#include <JuceHeader.h>

#include "engine/world.h"
#include "Frust/EngineFrustHost.h"
#include "Frust/PodCatalog.h"
#include "Interaction/EditorInteraction.h"
#include "Views/BehaviorAttachmentPanel.h"
#include "Views/MaterialsPanel.h"
#include "Views/PhysicsPropertiesPanel.h"
#include "Views/TransformPanel.h"

namespace ce {

// Editor UI/Workflow Overhaul plan, Phase 2: the one always-open,
// selection-driven panel replacing four previously-separate standing dock
// tabs (Transform, Material Inspector, Behaviors, Lighting) -- Lighting is
// NOT folded in here (it's global scene state, not per-entity, so it
// doesn't fit a per-selection panel at all; it becomes lazily-openable from
// the View menu instead, same treatment as Input Bindings).
//
// Composes the existing per-entity editors as child sections rather than
// rewriting their read/write logic -- each one already independently reads/
// writes against the same World with the same RegistryMutex discipline and
// SceneFlags::locked handling. This panel's only real job is: own them,
// forward SetSelectedEntity/Refresh to all of them at once, and stack them
// in a scrollable column (combined height easily exceeds a fixed dock
// column's height once Physics's variable-shape fields and an arbitrary
// number of attached Behaviors are both in play).
class PropertiesPanel final : public juce::Component {
public:
    PropertiesPanel(engine::World& world, interaction::EditorInteraction& interactions,
                    frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog);
    // Declared (not defaulted inline) and defined in the .cpp, after
    // ContentHost's full definition: content_ is a
    // std::unique_ptr<ContentHost> where ContentHost is only forward-
    // declared here -- same reason ImportPanel::~ImportPanel() and
    // BehaviorAttachmentPanel::~BehaviorAttachmentPanel() are out-of-line.
    ~PropertiesPanel() override;

    void SetSelectedEntity(entt::entity entity);
    void Refresh();

    // Editor UI/Workflow Overhaul plan, Phase 3: replaces HierarchyPanel's
    // old Delete button (removed along with Hierarchy itself) -- fired
    // right before the entity is actually destroyed, same timing
    // HierarchyPanel::onEntityDestroying used to provide, so
    // frustHost_.notifyObjectDestroyed still gets called with a still-valid
    // entity.
    std::function<void(entt::entity)> onEntityDestroying;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class ContentHost;

    void DeleteSelectedEntity();

    engine::World& world_;
    entt::entity selectedEntity_ = entt::null;

    juce::TextButton deleteObjectButton_{ "Delete Object" };
    juce::Viewport scrollView_;
    std::unique_ptr<ContentHost> content_;

    TransformPanel transformPanel_;
    MaterialsPanel materialsPanel_;
    PhysicsPropertiesPanel physicsPanel_;
    BehaviorAttachmentPanel behaviorAttachmentPanel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PropertiesPanel)
};

} // namespace ce
