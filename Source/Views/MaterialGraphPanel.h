#pragma once

#include <JuceHeader.h>
#include <creation/material/material_compiler.h>
#include <creation/material/material_nodes.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodeInspector.h>
#include <creation/node_editor_ui/NodePalette.h>

#include "node_system/graph.h"
#include "node_system/type_registry.h"

namespace ce { class ViewportComponent; }

namespace ce::views
{

// The real "Materials" dock panel (replacing MainComponent's old
// PlaceholderPanel stand-in): a node graph editor over ce::material's real
// compiler, reusing the same shared canvas/palette/inspector triplet
// FrustLogicPanel already proved out for FRust behavior graphs. See
// docs/MATERIAL_SYSTEM_PLAN.md and apps/CreationEngine/AGENTS.md's Do It
// Right Rule -- this is the actual editor, not a text-dump compiler demo.
//
// A material is a named, independent asset (AssetCatalog::GetOrCreate
// Material) -- it has no idea what mesh, if any, uses it, the same way
// opening a Material in a content browser never requires an object to be
// selected first. Compile saves onto the NAMED material you're editing;
// assigning that material to a mesh's slot is a deliberately separate
// action (Assign), matching how a real engine keeps "author a material"
// and "put it on this object" as two different operations rather than
// one panel baking a graph's output directly onto whatever asset name
// happens to be typed in. This was a real design mistake in an earlier
// pass (a "Target asset" field that compiled straight onto an embedded
// Material) -- fixed once it was pointed out, not layered around.
//
// A new graph is never blank -- it starts pre-wired with a medium-gray
// Constant Color feeding Material Output's baseColor, the same
// convention Unreal/Blender use (there's always something to look at and
// edit from, never an empty canvas).
//
// Scope note: this panel edits ONE graph in memory for the session. It
// does not yet persist/reload a DIFFERENT named material's own graph
// (node layout, wiring) -- Compile saves the compiled shader+parameters
// onto the named material's live Material object (which every mesh slot
// referencing it shares), but switching to edit a different existing
// material's graph from scratch is a separate, not-yet-built piece
// (would need each material's graph serialized via .frgraph and reloaded
// on open, not just its compiled output).
class MaterialGraphPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    explicit MaterialGraphPanel(ViewportComponent& viewport);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void buildDefaultGraph();
    void compileAndSave();
    void assignToMesh();

    ViewportComponent& viewport_;
    node_system::NodeTypeRegistry registry_;
    node_system::Graph graph_ { "Material", node_system::GraphTarget::Material };

    juce::Label title_ { {}, "Materials" };
    juce::Label hint_ { {}, "Drag nodes from the palette, wire them to Material Output, then compile." };
    juce::Label materialNameLabel_ { {}, "Material name" };
    juce::TextEditor materialName_;
    juce::TextButton compileButton_ { "Compile" };
    juce::Label assignLabel_ { {}, "Assign to mesh" };
    juce::TextEditor assignMeshName_;
    juce::TextButton assignButton_ { "Assign" };
    juce::Label status_;
    juce::TextEditor sourceView_;
    creation::node_editor_ui::NodePalette palette_;
    creation::node_editor_ui::NodeGraphComponent graphView_;
    creation::node_editor_ui::NodeInspector inspector_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaterialGraphPanel)
};

} // namespace ce::views
