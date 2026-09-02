#include "MaterialGraphPanel.h"

#include <algorithm>

#include "Render/ViewportComponent.h"

namespace ce::views
{

namespace
{
node_system::NodeTypeRegistry BuildMaterialRegistry()
{
    node_system::NodeTypeRegistry result;
    material::RegisterMaterialNodes(result);
    return result;
}
}

MaterialGraphPanel::MaterialGraphPanel(ViewportComponent& viewport)
    : viewport_(viewport),
      registry_(BuildMaterialRegistry()),
      palette_(registry_),
      graphView_(graph_, registry_),
      inspector_(graph_)
{
    title_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    title_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title_);

    hint_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hint_);

    materialNameLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(materialNameLabel_);

    materialName_.setText("M_New", juce::dontSendNotification);
    materialName_.setFont(juce::Font(juce::FontOptions(14.0f)));
    materialName_.setTooltip("Name of the material asset you're editing -- independent of any mesh. "
                              "Compile creates it if it doesn't exist yet, or updates it if it does.");
    addAndMakeVisible(materialName_);

    compileButton_.onClick = [this] { compileAndSave(); };
    compileButton_.setTooltip("Validate the graph, generate GLSL, and save it onto the named material above.");
    addAndMakeVisible(compileButton_);

    assignLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(assignLabel_);

    assignMeshName_.setText("Cube", juce::dontSendNotification);
    assignMeshName_.setFont(juce::Font(juce::FontOptions(14.0f)));
    assignMeshName_.setTooltip("Name of a catalog mesh asset whose material slot should point at the "
                                "material above -- a separate action from compiling it.");
    addAndMakeVisible(assignMeshName_);

    assignButton_.onClick = [this] { assignToMesh(); };
    assignButton_.setTooltip("Repoint this mesh asset's material slot to reference the named material above.");
    addAndMakeVisible(assignButton_);

    status_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(status_);

    sourceView_.setMultiLine(true);
    sourceView_.setReadOnly(true);
    sourceView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    sourceView_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0e1218));
    sourceView_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd7e3f0));
    sourceView_.setText("Compile to see the generated GLSL and any validation errors here.", juce::dontSendNotification);
    addAndMakeVisible(sourceView_);

    buildDefaultGraph();
    graphView_.GraphReplaced();

    graphView_.onSelectionChanged = [this](node_system::NodeId id) { inspector_.SetSelectedNode(id); };
    graphView_.onGraphChanged = [this] { status_.setText("Graph edited", juce::dontSendNotification); };
    addAndMakeVisible(palette_);
    addAndMakeVisible(graphView_);
    addAndMakeVisible(inspector_);

    status_.setText(juce::String(static_cast<int>(registry_.Types().size())) + " material nodes available", juce::dontSendNotification);
}

void MaterialGraphPanel::buildDefaultGraph()
{
    auto* colorNode = node_system::AddRegisteredNode(graph_, registry_, "material.constant.color");
    auto* outputNode = node_system::AddRegisteredNode(graph_, registry_, "material.surface.output");
    if (colorNode == nullptr || outputNode == nullptr) return;

    colorNode->SetEditorPosition(20.0f, 100.0f);
    outputNode->SetEditorPosition(240.0f, 100.0f);

    // Matches Surface Output's own unconnected-baseColor fallback (see
    // material_nodes.cpp) -- a plain medium gray, not an arbitrary choice.
    // Set on the INPUT pin (the editable literal NodeInspector shows) --
    // the output pin is structural only, see material_nodes.cpp.
    const auto valueInput = std::find_if(colorNode->Inputs().begin(), colorNode->Inputs().end(),
        [](const node_system::Pin& pin) { return pin.name == "value"; });
    if (valueInput != colorNode->Inputs().end())
        if (auto* colorValuePin = colorNode->FindPin(valueInput->id))
            colorValuePin->defaultValue = node_system::Vec3Default{ 0.8f, 0.8f, 0.8f };

    const auto baseColorInput = std::find_if(outputNode->Inputs().begin(), outputNode->Inputs().end(),
        [](const node_system::Pin& pin) { return pin.name == "baseColor"; });
    if (baseColorInput != outputNode->Inputs().end())
        graph_.Connect(colorNode->Id(), colorNode->Outputs().front().id, outputNode->Id(), baseColorInput->id);
}

void MaterialGraphPanel::compileAndSave()
{
    const auto materialNameText = materialName_.getText().trim();
    if (materialNameText.isEmpty()) {
        status_.setText("Name the material before compiling", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
        return;
    }

    const auto result = material::CompileMaterialGraph(graph_, registry_);
    if (!result.ok) {
        juce::String errorText;
        for (const auto& err : result.errors) errorText += juce::String(err) + "\n";
        sourceView_.setText(errorText.isNotEmpty() ? errorText : juce::String("Compile failed."), juce::dontSendNotification);
        status_.setText("Compile failed", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }

    // Both functions always ship together -- material_host.frag calls
    // EvaluateMaterial and ignores EvaluateWorldPositionOffset sitting
    // unused in its own compiled unit, material_host.vert does the
    // reverse. See MaterialShaderSource's own comments (material_compiler.h).
    const auto generatedSource = juce::String(result.source.declarations) + "\n" + juce::String(result.source.evaluateFunction)
                                + "\n" + juce::String(result.source.vertexFunction);
    sourceView_.setText(generatedSource, juce::dontSendNotification);

    // The material being edited has no idea what, if anything, uses it --
    // this looks it up (creating it if it's new) purely by name, with no
    // mesh/object in the picture at all. GetOrCreateMaterial always
    // returns the SAME shared_ptr for a given name, so every mesh slot
    // already assigned to this name (AssetCatalog::AssignMaterial) picks
    // up this update immediately: they share the identical object, not a
    // copy of it.
    auto material = viewport_.Catalog().GetOrCreateMaterial(materialNameText);

    material->compiledMaterialSource = generatedSource;

    // Recompiling replaces the parameter set wholesale, same as
    // compiledMaterialSource above -- any per-entity
    // engine::MaterialParameterOverrides (FRust-driven) are untouched,
    // since those live on the entity, not here.
    material->parameterFloatValues.clear();
    material->parameterColorValues.clear();
    for (const auto& param : result.source.parameters) {
        if (param.type == node_system::DataType::Color)
            material->parameterColorValues[param.name] = { param.defaultColor.x, param.defaultColor.y, param.defaultColor.z };
        else
            material->parameterFloatValues[param.name] = param.defaultFloat;
    }

    // Texture Sample nodes reference a file path (see material_nodes.cpp --
    // no asset picker yet), resolved here into a real GPU texture. Loading
    // needs a current GL context, which this message-thread button click
    // doesn't have -- hop via RunOnGLThread, same pattern Source/Import's
    // importers already use for the identical reason.
    material->textureBindings.clear();
    if (!result.source.textures.empty()) {
        juce::String missingPaths;
        viewport_.RunOnGLThread([&] {
            for (const auto& tex : result.source.textures) {
                auto texture = viewport_.Catalog().GetOrLoadTexture(juce::File(tex.path));
                if (texture == nullptr) {
                    missingPaths += juce::String(tex.path) + "\n";
                    continue;
                }
                material->textureBindings[tex.uniformName] = texture;
            }
        }, /*blockUntilFinished=*/true);
        if (missingPaths.isNotEmpty()) {
            status_.setText("Compiled, but couldn't load: " + missingPaths.trim(), juce::dontSendNotification);
            status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
            return;
        }
    }

    status_.setText("Compiled and saved \"" + materialNameText + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void MaterialGraphPanel::assignToMesh()
{
    const auto materialNameText = materialName_.getText().trim();
    const auto meshNameText = assignMeshName_.getText().trim();
    if (materialNameText.isEmpty() || meshNameText.isEmpty()) {
        status_.setText("Need both a material name and a mesh asset name to assign", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
        return;
    }

    if (!viewport_.Catalog().AssignMaterial(meshNameText, materialNameText)) {
        status_.setText("\"" + meshNameText + "\" isn't a known mesh asset", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }

    status_.setText("\"" + meshNameText + "\" now uses material \"" + materialNameText + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void MaterialGraphPanel::resized()
{
    auto area = getLocalBounds().reduced(16, 12);
    auto header = area.removeFromTop(48);
    title_.setBounds(header.removeFromTop(24));
    hint_.setBounds(header.removeFromTop(24));

    auto footer = area.removeFromBottom(150);
    auto footerHeader = footer.removeFromTop(26);
    materialNameLabel_.setBounds(footerHeader.removeFromLeft(90));
    materialName_.setBounds(footerHeader.removeFromLeft(120));
    footerHeader.removeFromLeft(8);
    compileButton_.setBounds(footerHeader.removeFromLeft(90));
    footerHeader.removeFromLeft(16);
    assignLabel_.setBounds(footerHeader.removeFromLeft(90));
    assignMeshName_.setBounds(footerHeader.removeFromLeft(120));
    footerHeader.removeFromLeft(8);
    assignButton_.setBounds(footerHeader.removeFromLeft(80));
    footerHeader.removeFromLeft(8);
    status_.setBounds(footerHeader);
    sourceView_.setBounds(footer.reduced(0, 4));
    area.removeFromBottom(8);

    auto left = area.removeFromLeft(190);
    palette_.setBounds(left);
    area.removeFromLeft(8);
    auto right = area.removeFromRight(220);
    inspector_.setBounds(right);
    area.removeFromRight(8);
    graphView_.setBounds(area);
}

void MaterialGraphPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10141a));
}

} // namespace ce::views
