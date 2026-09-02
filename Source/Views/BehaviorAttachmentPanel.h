#pragma once

#include <JuceHeader.h>
#include <entt/entt.hpp>

#include "engine/world.h"
#include "Frust/BehaviorCatalog.h"
#include "Frust/EngineFrustHost.h"

namespace ce {

// Per-selected-entity view of scene::BehaviorAttachments -- the "attach a
// Component to a Model" half of docs/BEHAVIOR_COMPONENT_MODEL.md. Lists
// currently attached pod IDs with Remove, and a picker to attach any
// compiled Behavior from BehaviorCatalog. Attaching a Behavior that
// hasn't been compiled yet (no loaded pod) is refused with a clear
// reason rather than attaching a pod ID nothing can actually load.
class BehaviorAttachmentPanel final : public juce::Component {
public:
    BehaviorAttachmentPanel(engine::World& world, frust::EngineFrustHost& frustHost, frust::BehaviorCatalog& catalog);

    // Declared (not defaulted inline) and defined in the .cpp, after
    // AttachmentRow's full definition: rows_ is a juce::OwnedArray<
    // AttachmentRow> where AttachmentRow is only forward-declared here --
    // same reason ImportPanel::~ImportPanel() is out-of-line.
    ~BehaviorAttachmentPanel() override;

    void SetSelectedEntity(entt::entity entity);
    void Refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class AttachmentRow;

    void AttachSelectedBehavior();
    void RemoveBehavior(const juce::String& podId);

    engine::World& world_;
    frust::EngineFrustHost& frustHost_;
    frust::BehaviorCatalog& catalog_;
    entt::entity selectedEntity_ = entt::null;

    juce::Label titleLabel_ { {}, "Behaviors" };
    juce::Label noSelectionLabel_ { {}, "No entity selected" };
    juce::ComboBox availableBehaviors_;
    juce::TextButton attachButton_ { "Attach" };
    juce::OwnedArray<AttachmentRow> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BehaviorAttachmentPanel)
};

} // namespace ce
