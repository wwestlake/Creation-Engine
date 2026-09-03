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

// Pod editor: a browsable list of named Pods (PodCatalog), grouped by
// Kind (Behavior/Processing), opened one at a time for editing -- same
// two-mode Browse/Edit shape MaterialGraphPanel was designed around,
// see docs/BEHAVIOR_COMPONENT_MODEL.md and the Pod Management System
// plan generalizing it. The graph IS the saved model; Compile generates
// FRust, writes it as a cached loadable pod, and loads it into
// EngineFrustHost -- it does not merely display generated text.
//
// A Pod's own identity/characteristics (name, Kind, Interface, the
// selected node's properties) live in a separate, independently
// dockable PodInfoPanel, not here -- this panel is graph canvas +
// palette + Save/Compile only. Pod Editor UX & Architecture Fixes plan
// Phase 6.
class PodEditorPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    PodEditorPanel(frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog,
                   creation::assets::ProjectSession& projectSession);

    // Declared (not defaulted inline) and defined in the .cpp, after
    // PodRow's full definition -- same reason ImportPanel::~ImportPanel()
    // and BehaviorAttachmentPanel::~BehaviorAttachmentPanel() are out-of-line.
    ~PodEditorPanel() override;

    // Opens an existing Pod straight into Edit mode -- public so
    // ContentBrowserPanel's click-to-open (wired in MainComponent) can
    // reach it without going through this panel's own Browse-mode list.
    void OpenPod(const juce::String& name);

    node_system::Graph& Graph() { return graph_; }
    const node_system::NodeTypeRegistry& Registry() const { return registry_; }

    // Fired whenever the open Pod changes -- a real name on open, or
    // empty when Browse mode is shown (no Pod open). PodInfoPanel (owned
    // by MainComponent, wired alongside this) uses it to refresh its
    // Pod-info section; this panel no longer shows that itself.
    std::function<void(juce::String)> onOpenPodChanged;
    // Fired on every graph selection change, including to "none" (id 0).
    // Same reasoning as onOpenPodChanged -- the selected node's property
    // editor lives in PodInfoPanel now, not here.
    std::function<void(node_system::NodeId)> onSelectedNodeChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void RefreshBrowseList();
    void CreatePod(frust::PodKind kind);
    void ShowBrowseMode();
    void ShowEditMode();
    void SaveContent();
    void CompileAndLoad();
    void InsertSnippet();

    frust::EngineFrustHost& frustHost_;
    frust::PodCatalog& catalog_;
    creation::assets::ProjectSession& projectSession_;

    node_system::Graph graph_ { "untitled", node_system::GraphTarget::Behavior };
    node_system::NodeTypeRegistry registry_;
    juce::String openName_;

    // --- Browse mode -- flat Kind-grouped sections, no folder tree
    // (Pod Management System plan's v1 scope decision). Creation is
    // Kind-only -- no name-entry box (UX objection, Phase 6): a default
    // name is generated and the Pod opens straight into Edit mode, where
    // PodInfoPanel is where you'd actually rename/configure it. ---
    juce::Label browseTitle_ { {}, "Pods" };
    juce::TextButton newBehaviorPodButton_ { "New Behavior Pod" };
    juce::TextButton newProcessingPodButton_ { "New Processing Pod" };
    // Graph vs hand-typed FRust source (Phase 8) -- both authoring modes
    // feed the same save/compile/reflect pipeline, this just picks which
    // one a newly created Pod starts as.
    juce::ComboBox newAuthoringModeCombo_;
    juce::Label behaviorSectionLabel_ { {}, "Behavior Pods" };
    juce::Label processingSectionLabel_ { {}, "Processing Pods" };
    class PodRow;
    juce::OwnedArray<PodRow> behaviorRows_;
    juce::OwnedArray<PodRow> processingRows_;

    // --- Edit mode ---
    juce::TextButton backButton_ { "< Pods" };
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

    bool editing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodEditorPanel)
};

} // namespace ce::views
