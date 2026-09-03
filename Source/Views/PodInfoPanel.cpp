#include "PodInfoPanel.h"

#include <algorithm>

namespace ce::views
{

namespace
{
juce::String KindLabel(frust::PodKind kind) { return kind == frust::PodKind::Processing ? "Processing" : "Behavior"; }

juce::String DataTypeLabel(node_system::DataType type) {
    switch (type) {
        case node_system::DataType::Float: return "Float";
        case node_system::DataType::Bool: return "Bool";
        case node_system::DataType::String: return "String";
        case node_system::DataType::Entity: return "Entity";
        case node_system::DataType::Transform: return "Transform";
        case node_system::DataType::Material: return "Material";
        case node_system::DataType::Model: return "Model";
        case node_system::DataType::Controller: return "Controller";
        default: return "Int";
    }
}

// Order here must match kTypesByIndex in AddInterfaceInput() below --
// both are indexed by newInputTypeCombo_'s selected item.
constexpr const char* kInterfaceTypeNames[] = {
    "Float", "Bool", "Int", "String", "Entity", "Transform", "Material", "Model", "Controller"
};

// Finds a Data pin on `node` matching `type` -- searches inputs (for
// binding a declared interface input to something the graph can feed a
// parameter into) or outputs (for binding the single interface output).
// Inputs additionally require the pin to be currently unwired, since a
// wired input already gets its value from the graph itself.
const node_system::Pin* FindBindablePin(const node_system::Graph& graph, const node_system::Node& node,
                                         node_system::DataType type, bool wantInput) {
    const auto& pins = wantInput ? node.Inputs() : node.Outputs();
    for (const auto& pin : pins) {
        if (pin.type.kind != node_system::PinKind::Data || pin.type.dataType != type) continue;
        if (wantInput) {
            const bool wired = std::any_of(graph.Connections().begin(), graph.Connections().end(),
                                            [&](const node_system::Connection& c) { return c.toNode == node.Id() && c.toPin == pin.id; });
            if (wired) continue;
        }
        return &pin;
    }
    return nullptr;
}
}

// One row in the Interface editor's input list: name/type, bound status
// (which node+pin currently feeds it, or "(unbound)" -- never a block,
// just a visible state per the plan's never-block editing philosophy),
// a Bind-to-selected button, and Remove.
class PodInfoPanel::InterfaceInputRow final : public juce::Component {
public:
    InterfaceInputRow(PodInfoPanel& owner, int index, juce::String label, juce::String boundStatus)
        : owner_(owner), index_(index) {
        nameLabel_.setText(label, juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        statusLabel_.setText(boundStatus, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        statusLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(statusLabel_);

        bindButton_.onClick = [this] { owner_.BindInterfaceInputAt(index_); };
        addAndMakeVisible(bindButton_);
        removeButton_.onClick = [this] { owner_.RemoveInterfaceInputAt(index_); };
        addAndMakeVisible(removeButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        removeButton_.setBounds(bounds.removeFromRight(24).reduced(1));
        bindButton_.setBounds(bounds.removeFromRight(44).reduced(1));
        nameLabel_.setBounds(bounds.removeFromTop(bounds.getHeight() / 2));
        statusLabel_.setBounds(bounds);
    }

private:
    PodInfoPanel& owner_;
    int index_;
    juce::Label nameLabel_;
    juce::Label statusLabel_;
    juce::TextButton bindButton_ { "Bind" };
    juce::TextButton removeButton_ { "x" };
};

PodInfoPanel::PodInfoPanel(frust::PodCatalog& catalog, creation::assets::ProjectSession& projectSession,
                           node_system::Graph& graph, const node_system::NodeTypeRegistry& registry)
    : catalog_(catalog), projectSession_(projectSession), graph_(graph), registry_(registry), inspector_(graph_, &registry_)
{
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noPodLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(noPodLabel_);

    nameLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addChildComponent(nameLabel_);

    kindModeLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    kindModeLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    addChildComponent(kindModeLabel_);

    exposeAsNodeToggle_.setTooltip("Make this Pod's compiled output usable as a node inside other graphs.");
    exposeAsNodeToggle_.onClick = [this] {
        if (openName_.isEmpty()) return;
        catalog_.SetExposeAsNode(openName_, exposeAsNodeToggle_.getToggleState());
    };
    addChildComponent(exposeAsNodeToggle_);

    interfaceLabel_.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
    interfaceLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addChildComponent(interfaceLabel_);

    newInputNameEditor_.setTextToShowWhenEmpty("Input name...", juce::Colours::grey);
    addChildComponent(newInputNameEditor_);
    {
        juce::StringArray typeItems;
        for (const auto* name : kInterfaceTypeNames) typeItems.add(name);
        newInputTypeCombo_.addItemList(typeItems, 1);
    }
    newInputTypeCombo_.setSelectedItemIndex(0, juce::dontSendNotification);
    addChildComponent(newInputTypeCombo_);
    addInputButton_.onClick = [this] { AddInterfaceInput(); };
    addChildComponent(addInputButton_);

    outputRowLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    outputRowLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    addChildComponent(outputRowLabel_);
    bindOutputButton_.setTooltip("Bind the Pod's single output to the selected node's data output.");
    bindOutputButton_.onClick = [this] { BindInterfaceOutput(); };
    addChildComponent(bindOutputButton_);

    addChildComponent(inspector_);

    RefreshPodInfo();
}

PodInfoPanel::~PodInfoPanel() = default;

void PodInfoPanel::SetOpenPod(juce::String name) {
    openName_ = std::move(name);
    selectedNodeId_ = 0;
    inspector_.SetSelectedNode(0);
    RefreshPodInfo();
}

void PodInfoPanel::SetSelectedNode(node_system::NodeId nodeId) {
    selectedNodeId_ = nodeId;
    inspector_.SetSelectedNode(nodeId);
}

void PodInfoPanel::RefreshPodInfo() {
    const bool hasPod = openName_.isNotEmpty();
    noPodLabel_.setVisible(!hasPod);
    nameLabel_.setVisible(hasPod);
    kindModeLabel_.setVisible(hasPod);

    const bool isSource = hasPod && catalog_.AuthoringMode(openName_) == frust::PodAuthoringMode::Source;
    exposeAsNodeToggle_.setVisible(hasPod && !isSource);
    interfaceLabel_.setVisible(hasPod && !isSource);
    newInputNameEditor_.setVisible(hasPod && !isSource);
    newInputTypeCombo_.setVisible(hasPod && !isSource);
    addInputButton_.setVisible(hasPod && !isSource);
    outputRowLabel_.setVisible(hasPod && !isSource);
    bindOutputButton_.setVisible(hasPod && !isSource);
    inspector_.setVisible(hasPod && !isSource);

    if (!hasPod) {
        interfaceInputRows_.clear();
        resized();
        return;
    }

    const auto kind = catalog_.Kind(openName_);
    nameLabel_.setText(openName_, juce::dontSendNotification);
    kindModeLabel_.setText(KindLabel(kind) + (isSource ? " / Source" : " / Graph"), juce::dontSendNotification);
    exposeAsNodeToggle_.setToggleState(catalog_.ExposeAsNode(openName_), juce::dontSendNotification);

    RefreshInterfaceRows();
}

void PodInfoPanel::AddInterfaceInput() {
    if (openName_.isEmpty()) return;
    const auto name = newInputNameEditor_.getText().trim();
    if (name.isEmpty()) return;
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr) return;

    static constexpr node_system::DataType kTypesByIndex[] = {
        node_system::DataType::Float, node_system::DataType::Bool, node_system::DataType::Int, node_system::DataType::String,
        node_system::DataType::Entity, node_system::DataType::Transform, node_system::DataType::Material,
        node_system::DataType::Model, node_system::DataType::Controller
    };
    const int typeIndex = newInputTypeCombo_.getSelectedItemIndex();

    frust::PodInterfaceInput input;
    input.name = name;
    input.type = (typeIndex >= 0 && static_cast<size_t>(typeIndex) < std::size(kTypesByIndex))
                      ? kTypesByIndex[typeIndex] : node_system::DataType::Int;
    entry->interfaceInputs.push_back(input);

    newInputNameEditor_.clear();
    RefreshInterfaceRows();
}

void PodInfoPanel::BindInterfaceInputAt(int index) {
    if (openName_.isEmpty() || selectedNodeId_ == 0) return;
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr || index < 0 || static_cast<size_t>(index) >= entry->interfaceInputs.size()) return;
    const auto* node = graph_.FindNode(selectedNodeId_);
    if (node == nullptr) return;

    auto& input = entry->interfaceInputs[static_cast<size_t>(index)];
    const auto* pin = FindBindablePin(graph_, *node, input.type, /*wantInput=*/true);
    if (pin == nullptr) return;
    input.boundNode = selectedNodeId_;
    input.boundPin = pin->id;
    RefreshInterfaceRows();
}

void PodInfoPanel::RemoveInterfaceInputAt(int index) {
    if (openName_.isEmpty()) return;
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr || index < 0 || static_cast<size_t>(index) >= entry->interfaceInputs.size()) return;
    entry->interfaceInputs.erase(entry->interfaceInputs.begin() + index);
    RefreshInterfaceRows();
}

void PodInfoPanel::BindInterfaceOutput() {
    if (openName_.isEmpty() || selectedNodeId_ == 0) return;
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr) return;
    const auto* node = graph_.FindNode(selectedNodeId_);
    if (node == nullptr) return;

    const auto output = std::find_if(node->Outputs().begin(), node->Outputs().end(),
                                      [](const node_system::Pin& pin) { return pin.type.kind == node_system::PinKind::Data; });
    if (output == node->Outputs().end()) return;
    entry->outputNode = selectedNodeId_;
    entry->outputPin = output->id;
    RefreshInterfaceRows();
}

void PodInfoPanel::RefreshInterfaceRows() {
    interfaceInputRows_.clear();
    if (openName_.isEmpty()) { resized(); return; }
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr) { resized(); return; }

    // Unbound is a visible state, never a block -- see the plan's
    // never-block editing philosophy (a Component with a required input
    // unwired renders as an error, not a refused action).
    auto boundStatusFor = [this](node_system::NodeId nodeId, node_system::PinId pinId) -> juce::String {
        if (nodeId == 0) return "(unbound)";
        const auto* node = graph_.FindNode(nodeId);
        const auto* pin = node ? node->FindPin(pinId) : nullptr;
        return pin ? "-> " + juce::String(node->TypeName()) + "." + pin->name : "(bound node no longer exists)";
    };

    for (size_t i = 0; i < entry->interfaceInputs.size(); ++i) {
        const auto& input = entry->interfaceInputs[i];
        const auto label = input.name + " : " + DataTypeLabel(input.type);
        auto* row = interfaceInputRows_.add(new InterfaceInputRow(*this, static_cast<int>(i), label,
                                                                    boundStatusFor(input.boundNode, input.boundPin)));
        addAndMakeVisible(row);
    }

    outputRowLabel_.setText("Output: " + boundStatusFor(entry->outputNode, entry->outputPin), juce::dontSendNotification);
    resized();
}

void PodInfoPanel::resized() {
    auto area = getLocalBounds().reduced(12, 10);

    titleLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    if (openName_.isEmpty()) {
        noPodLabel_.setBounds(area.removeFromTop(20));
        return;
    }

    nameLabel_.setBounds(area.removeFromTop(20));
    kindModeLabel_.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);

    // Source Pods have no graph selection to inspect below.
    if (catalog_.AuthoringMode(openName_) == frust::PodAuthoringMode::Source) return;

    exposeAsNodeToggle_.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    interfaceLabel_.setBounds(area.removeFromTop(18));
    auto addRow = area.removeFromTop(24);
    addInputButton_.setBounds(addRow.removeFromRight(70).reduced(1));
    newInputTypeCombo_.setBounds(addRow.removeFromRight(70).reduced(1));
    addRow.removeFromRight(4);
    newInputNameEditor_.setBounds(addRow);
    area.removeFromTop(4);
    for (auto* row : interfaceInputRows_) {
        row->setBounds(area.removeFromTop(32));
        area.removeFromTop(2);
    }
    area.removeFromTop(6);
    outputRowLabel_.setBounds(area.removeFromTop(16));
    bindOutputButton_.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);

    inspector_.setBounds(area);
}

void PodInfoPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10141a));
}

} // namespace ce::views
