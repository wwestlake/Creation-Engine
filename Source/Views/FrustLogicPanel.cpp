#include "FrustLogicPanel.h"

#include <algorithm>

namespace ce::views
{

namespace
{
node_system::NodeTypeRegistry CopyRegistry(const node_system::NodeLibraryRegistry& libraries)
{
    node_system::NodeTypeRegistry result;
    for (const auto& [name, descriptor] : libraries.TypeRegistry().Types())
        result.Register(descriptor);
    return result;
}
}

FrustLogicPanel::FrustLogicPanel(const node_system::NodeLibraryRegistry& libraries)
    : libraries_(libraries),
      registry_(CopyRegistry(libraries)),
      palette_(registry_),
      graphView_(graph_, registry_),
      inspector_(graph_)
{
    title_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    title_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title_);

    hint_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hint_);

    compileButton_.onClick = [this] { compileGraph(); };
    compileButton_.setTooltip("Validate the graph and generate a FRust function");
    addAndMakeVisible(compileButton_);

    status_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(status_);

    sourceView_.setMultiLine(true);
    sourceView_.setReadOnly(true);
    sourceView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    sourceView_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0e1218));
    sourceView_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd7e3f0));
    sourceView_.setText("Compile FRust to see generated source and validation errors here.", juce::dontSendNotification);
    addAndMakeVisible(sourceView_);

    graphView_.onSelectionChanged = [this](node_system::NodeId id) { inspector_.SetSelectedNode(id); };
    graphView_.onGraphChanged = [this] { status_.setText("Graph edited", juce::dontSendNotification); };
    addAndMakeVisible(palette_);
    addAndMakeVisible(graphView_);
    addAndMakeVisible(inspector_);

    status_.setText(juce::String(static_cast<int>(registry_.Types().size())) + " FRust nodes available", juce::dontSendNotification);
}

void FrustLogicPanel::compileGraph()
{
    node_system::FrustGraphCompileOptions options;
    options.functionName = "evaluate_logic";
    options.manifestJson = "{\"name\":\"engine_logic_graph\",\"version\":\"0.1.0\"}";
    for (const auto& [id, library] : libraries_.Libraries())
        options.sourceModules.insert(options.sourceModules.end(), library.frustSourceModules.begin(), library.frustSourceModules.end());

    for (const auto& [id, node] : graph_.Nodes())
    {
        if (!node->Outputs().empty() && node->Outputs().front().type.kind == node_system::PinKind::Data)
        {
            options.resultNode = id;
            options.resultPin = node->Outputs().front().id;
        }
    }

    if (options.resultNode == 0)
    {
        sourceView_.setText("Add a data-producing node to compile this graph.", juce::dontSendNotification);
        status_.setText("Nothing to compile", juce::dontSendNotification);
        return;
    }

    const auto result = node_system::CompileBehaviorGraphToFrust(graph_, libraries_, options);
    sourceView_.setText(juce::String(result.ok ? result.source : result.error), juce::dontSendNotification);
    status_.setText(result.ok ? "Compiled successfully" : "Compile failed: " + juce::String(result.error), juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, result.ok ? juce::Colour(0xff67e8a5) : juce::Colour(0xffff6b6b));
}

void FrustLogicPanel::resized()
{
    auto area = getLocalBounds().reduced(16, 12);
    auto header = area.removeFromTop(48);
    title_.setBounds(header.removeFromTop(24));
    hint_.setBounds(header.removeFromTop(24));
    auto footer = area.removeFromBottom(150);
    auto footerHeader = footer.removeFromTop(26);
    compileButton_.setBounds(footerHeader.removeFromLeft(130));
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

void FrustLogicPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10141a));
}

} // namespace ce::views
