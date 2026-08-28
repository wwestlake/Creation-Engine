#pragma once

#include <JuceHeader.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodeInspector.h>
#include <creation/node_editor_ui/NodePalette.h>

#include "node_system/frgraph_serialization.h"
#include "node_system/frust_codegen.h"
#include "node_system/node_library.h"

namespace ce::engine { class World; }

namespace ce::views
{

class FrustLogicPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit FrustLogicPanel(const node_system::NodeLibraryRegistry& libraries);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void compileGraph();

    const node_system::NodeLibraryRegistry& libraries_;
    node_system::Graph graph_ { "FRust Logic", node_system::GraphTarget::Behavior };
    node_system::NodeTypeRegistry registry_;

    juce::Label title_ { {}, "FRust Logic" };
    juce::Label hint_ { {}, "Drag nodes from the palette, connect pins, then compile to FRust." };
    juce::TextButton compileButton_ { "Compile FRust" };
    juce::Label status_;
    juce::TextEditor sourceView_;
    creation::node_editor_ui::NodePalette palette_;
    creation::node_editor_ui::NodeGraphComponent graphView_;
    creation::node_editor_ui::NodeInspector inspector_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrustLogicPanel)
};

} // namespace ce::views
