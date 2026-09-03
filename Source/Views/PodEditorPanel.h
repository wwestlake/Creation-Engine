#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodePalette.h>

#include "Frust/EngineFrustHost.h"
#include "Frust/PodCatalog.h"
#include "node_system/core_control_flow.h"
#include "node_system/frgraph_serialization.h"
#include "node_system/frust_codegen.h"
#include "node_system/node_library.h"

namespace ce::engine { class World; }

namespace ce::views
{

// Pod editor: a workspace for whatever ONE Pod is currently open in it --
// graph canvas, palette, Save/Compile. Nothing else. There is no browse
// list, no "New Pod" UI, and no state for "no Pod open" to render: this
// panel's dock tab does not exist at all unless a Pod is open in it (see
// MainComponent::EnsurePodPanelsOpen), and closing it unregisters the tab
// entirely rather than leaving an empty editor sitting around. Discovery
// and creation both live in ContentBrowserPanel's Pods section instead --
// right-click there -> New Behavior/Processing Pod. A Pod's own identity/
// characteristics (name, Kind, Interface, the selected node's properties)
// live in the separate, independently dockable PodInfoPanel, also opened
// and closed in lockstep with this one. Pod/Asset Workflow plan Phase 4
// (this panel), Phase 5 (the dock lifecycle around it).
class PodEditorPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    PodEditorPanel(frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog,
                   creation::assets::ProjectSession& projectSession);
    ~PodEditorPanel() override = default;

    // The only way anything ever appears in this panel -- called by
    // MainComponent from ContentBrowserPanel's onAssetOpened/onPodCreated.
    void OpenPod(const juce::String& name);

    node_system::Graph& Graph() { return graph_; }
    const node_system::NodeTypeRegistry& Registry() const { return registry_; }

    // Fired whenever the open Pod changes -- PodInfoPanel uses it to
    // refresh its Pod-info section.
    std::function<void(juce::String)> onOpenPodChanged;
    // Fired on every graph selection change, including to "none" (id 0).
    // The selected node's property editor lives in PodInfoPanel, not here.
    std::function<void(node_system::NodeId)> onSelectedNodeChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void SaveContent();
    void CompileAndLoad();
    void InsertSnippet();

    frust::EngineFrustHost& frustHost_;
    frust::PodCatalog& catalog_;
    creation::assets::ProjectSession& projectSession_;

    node_system::Graph graph_ { "untitled", node_system::GraphTarget::Behavior };
    node_system::NodeTypeRegistry registry_;
    juce::String openName_;

    juce::Label editTitle_ { {}, "Pod Editor" };
    juce::Label hint_ { {}, "Drag nodes from the palette, connect pins, then Save and Compile." };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton compileButton_ { "Compile" };
    juce::Label status_;
    juce::TextEditor sourceView_;
    creation::node_editor_ui::NodePalette palette_;
    creation::node_editor_ui::NodeGraphComponent graphView_;

    // --- Source Pod editing (Phase 8) -- shown instead of
    // palette_/graphView_ when the open Pod's authoring mode is Source.
    // A Source Pod's model IS its FRust text directly; Compile skips the
    // graph-to-text step entirely. ---
    juce::CodeDocument sourceCodeDocument_;
    std::unique_ptr<juce::CodeTokeniser> frustTokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> sourceEditor_;
    juce::ComboBox snippetCombo_;
    juce::TextButton insertSnippetButton_ { "Insert" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodEditorPanel)
};

} // namespace ce::views
