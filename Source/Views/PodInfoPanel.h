#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <creation/node_editor_ui/NodeInspector.h>

#include "Frust/PodCatalog.h"
#include "node_system/graph.h"
#include "node_system/type_registry.h"

namespace ce::views
{

// A Pod's identity as a real thing, not just a name on a canvas: this
// panel is what's actually docked and visible when you open a Pod in
// the designer, per the user's explicit objection that "this diagram
// [the graph canvas] is lost, it has no anchor... it's not a thing,
// it's a place for nodes." Pod at the top (name/Kind/authoring mode,
// expose-as-node, Interface inputs/output), selected node's property
// editor below -- one dockable/undockable/movable panel, following
// the same "PodEditorPanel is the graph canvas + palette only" split
// PodEditorPanel.h's own header comment describes.
//
// Owned by MainComponent, wired to PodEditorPanel's onOpenPodChanged/
// onSelectedNodeChanged callbacks -- the same fan-out-from-a-callback
// shape MainComponent already uses for hierarchyPanel_.onSelectionChanged
// driving transformPanel_/pbrMaterialPanel_/behaviorAttachmentPanel_.
// Registered via DockManager::registerPanel like every other panel, so
// undock/float support is free, not reimplemented here. Pod Editor UX &
// Architecture Fixes plan Phase 6.
class PodInfoPanel final : public juce::Component
{
public:
    PodInfoPanel(frust::PodCatalog& catalog, creation::assets::ProjectSession& projectSession,
                 node_system::Graph& graph, const node_system::NodeTypeRegistry& registry);
    ~PodInfoPanel() override;

    // name is empty when no Pod is open (Browse mode) -- shows a plain
    // "No Pod open" placeholder rather than stale info from the last
    // Pod. graph is assumed already updated to reflect the newly open
    // Pod by the time this is called (PodEditorPanel::OpenPod fires
    // onOpenPodChanged after graph_ is swapped in).
    void SetOpenPod(juce::String name);
    void SetSelectedNode(node_system::NodeId nodeId);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void RefreshPodInfo();
    void AddInterfaceInput();
    void BindInterfaceInputAt(int index);
    void RemoveInterfaceInputAt(int index);
    void BindInterfaceOutput();
    void RefreshInterfaceRows();

    frust::PodCatalog& catalog_;
    creation::assets::ProjectSession& projectSession_;
    node_system::Graph& graph_;
    const node_system::NodeTypeRegistry& registry_;

    juce::String openName_;
    node_system::NodeId selectedNodeId_ = 0;

    // --- Pod info section ---
    juce::Label titleLabel_ { {}, "Pod" };
    juce::Label noPodLabel_ { {}, "No Pod open -- open or create one in the Pods panel." };
    juce::Label nameLabel_;
    juce::Label kindModeLabel_;
    juce::ToggleButton exposeAsNodeToggle_ { "Expose as node" };

    juce::Label interfaceLabel_ { {}, "Interface" };
    juce::TextEditor newInputNameEditor_;
    juce::ComboBox newInputTypeCombo_;
    juce::TextButton addInputButton_ { "+ Add Input" };
    class InterfaceInputRow;
    juce::OwnedArray<InterfaceInputRow> interfaceInputRows_;
    juce::Label outputRowLabel_;
    juce::TextButton bindOutputButton_ { "Bind Output to Selected" };

    // --- Selected-node section ---
    creation::node_editor_ui::NodeInspector inspector_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodInfoPanel)
};

} // namespace ce::views
