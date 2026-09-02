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
    void SaveGraph();
    void CompileAndLoad();

    frust::EngineFrustHost& frustHost_;
    frust::PodCatalog& catalog_;
    creation::assets::ProjectSession& projectSession_;

    node_system::Graph graph_ { "untitled", node_system::GraphTarget::Behavior };
    node_system::NodeTypeRegistry registry_;
    juce::String openName_;

    // --- Browse mode -- flat Kind-grouped sections, no folder tree
    // (Pod Management System plan's v1 scope decision). ---
    juce::Label browseTitle_ { {}, "Pods" };
    juce::TextEditor newNameEditor_;
    juce::TextButton newBehaviorPodButton_ { "New Behavior Pod" };
    juce::TextButton newProcessingPodButton_ { "New Processing Pod" };
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

    bool editing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PodEditorPanel)
};

} // namespace ce::views
