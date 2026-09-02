#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodeInspector.h>
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
class PodEditorPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    PodEditorPanel(frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog,
                   creation::assets::ProjectSession& projectSession);

    // Declared (not defaulted inline) and defined in the .cpp, after
    // PodRow's full definition -- same reason ImportPanel::~ImportPanel()
    // and BehaviorAttachmentPanel::~BehaviorAttachmentPanel() are out-of-line.
    ~PodEditorPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void RefreshBrowseList();
    void CreatePod(frust::PodKind kind);
    void OpenPod(const juce::String& name);
    void ShowBrowseMode();
    void ShowEditMode();
    void SaveContent();
    void CompileAndLoad();
    void AddInterfaceInput();
    void BindInterfaceInputAt(int index);
    void RemoveInterfaceInputAt(int index);
    void BindInterfaceOutput();
    void RefreshInterfaceRows();
    void InsertSnippet();

    frust::EngineFrustHost& frustHost_;
    frust::PodCatalog& catalog_;
    creation::assets::ProjectSession& projectSession_;

    node_system::Graph graph_ { "untitled", node_system::GraphTarget::Behavior };
    node_system::NodeTypeRegistry registry_;
    juce::String openName_;
    // Tracks the graph canvas's current selection -- reused by the
    // interface editor's "Bind to Selected" buttons (Phase 6) so binding
    // a declared input/output pin doesn't need its own separate pin-pick
    // UI, just the same selection wiring NodeInspector already consumes.
    node_system::NodeId selectedNodeId_ = 0;

    // --- Browse mode -- flat Kind-grouped sections, no folder tree
    // (Pod Management System plan's v1 scope decision). ---
    juce::Label browseTitle_ { {}, "Pods" };
    juce::TextEditor newNameEditor_;
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
    juce::ToggleButton exposeAsNodeToggle_ { "Expose as node" };
    juce::Label status_;
    juce::TextEditor sourceView_;
    creation::node_editor_ui::NodePalette palette_;
    creation::node_editor_ui::NodeGraphComponent graphView_;
    creation::node_editor_ui::NodeInspector inspector_;

    // --- Interface editor (Phase 6) -- explicit, author-declared typed
    // inputs/outputs, replacing the auto-detect resultNode/entryNode
    // heuristic. Single output for v1 (plan's scope decision). Shares
    // the right-hand column with inspector_. ---
    juce::Label interfaceLabel_ { {}, "Interface" };
    juce::TextEditor newInputNameEditor_;
    juce::ComboBox newInputTypeCombo_;
    juce::TextButton addInputButton_ { "+ Add Input" };
    class InterfaceInputRow;
    juce::OwnedArray<InterfaceInputRow> interfaceInputRows_;
    juce::Label outputRowLabel_;
    juce::TextButton bindOutputButton_ { "Bind Output to Selected" };

    // --- Source Pod editing (Phase 8) -- shown instead of
    // palette_/graphView_/inspector_/the interface editor when the open
    // Pod's authoring mode is Source. A Source Pod's model IS its FRust
    // text directly; Compile skips the graph-to-text step entirely. ---
    juce::CodeDocument sourceCodeDocument_;
    std::unique_ptr<juce::CodeTokeniser> frustTokeniser_;
    std::unique_ptr<juce::CodeEditorComponent> sourceEditor_;
    juce::ComboBox snippetCombo_;
    juce::TextButton insertSnippetButton_ { "Insert" };

    bool editing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodEditorPanel)
};

} // namespace ce::views
