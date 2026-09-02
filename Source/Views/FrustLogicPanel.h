#pragma once

#include <JuceHeader.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodeInspector.h>
#include <creation/node_editor_ui/NodePalette.h>

#include "Frust/BehaviorCatalog.h"
#include "Frust/EngineFrustHost.h"
#include "node_system/frgraph_serialization.h"
#include "node_system/frust_codegen.h"
#include "node_system/node_library.h"

namespace ce::engine { class World; }

namespace ce::views
{

// Behavior editor: a browsable list of named graphs (BehaviorCatalog),
// opened one at a time for editing -- same two-mode Browse/Edit shape
// MaterialGraphPanel was designed around, see
// docs/BEHAVIOR_COMPONENT_MODEL.md. The graph IS the saved model; Compile
// generates FRust, writes it as a cached loadable pod, and loads it into
// EngineFrustHost -- it does not merely display generated text the way
// this panel used to.
class FrustLogicPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    FrustLogicPanel(frust::EngineFrustHost& frustHost, frust::BehaviorCatalog& catalog);

    // Declared (not defaulted inline) and defined in the .cpp, after
    // BehaviorRow's full definition -- same reason ImportPanel::~ImportPanel()
    // and BehaviorAttachmentPanel::~BehaviorAttachmentPanel() are out-of-line.
    ~FrustLogicPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void RefreshBrowseList();
    void OpenBehavior(const juce::String& name);
    void ShowBrowseMode();
    void ShowEditMode();
    void SaveGraph();
    void CompileAndLoad();

    frust::EngineFrustHost& frustHost_;
    frust::BehaviorCatalog& catalog_;

    node_system::Graph graph_ { "untitled", node_system::GraphTarget::Behavior };
    node_system::NodeTypeRegistry registry_;
    juce::String openName_;

    // --- Browse mode ---
    juce::Label browseTitle_ { {}, "Behaviors" };
    juce::TextEditor newNameEditor_;
    juce::TextButton newBehaviorButton_ { "New Behavior" };
    class BehaviorRow;
    juce::OwnedArray<BehaviorRow> rows_;

    // --- Edit mode ---
    juce::TextButton backButton_ { "< Behaviors" };
    juce::Label editTitle_ { {}, "FRust Logic" };
    juce::Label hint_ { {}, "Drag nodes from the palette, connect pins, then Save and Compile." };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton compileButton_ { "Compile" };
    juce::Label status_;
    juce::TextEditor sourceView_;
    creation::node_editor_ui::NodePalette palette_;
    creation::node_editor_ui::NodeGraphComponent graphView_;
    creation::node_editor_ui::NodeInspector inspector_;

    bool editing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrustLogicPanel)
};

} // namespace ce::views
