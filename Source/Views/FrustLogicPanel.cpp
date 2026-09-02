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

// One row in Browse mode: a Behavior's name plus Open. Deliberately no
// Delete here yet -- see docs/BEHAVIOR_COMPONENT_MODEL.md's open asset-
// tracking-depth question; a real delete needs the same dependency-check
// treatment ContentBrowserPanel gives project assets, which Behaviors
// aren't tracked as yet.
class FrustLogicPanel::BehaviorRow final : public juce::Component {
public:
    BehaviorRow(FrustLogicPanel& owner, juce::String name) : owner_(owner), name_(std::move(name)) {
        nameLabel_.setText(name_, juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        openButton_.onClick = [this] { owner_.OpenBehavior(name_); };
        addAndMakeVisible(openButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        openButton_.setBounds(bounds.removeFromRight(70).reduced(2));
        nameLabel_.setBounds(bounds);
    }

private:
    FrustLogicPanel& owner_;
    juce::String name_;
    juce::Label nameLabel_;
    juce::TextButton openButton_ { "Open" };
};

FrustLogicPanel::~FrustLogicPanel() = default;

FrustLogicPanel::FrustLogicPanel(frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog)
    : frustHost_(frustHost),
      catalog_(catalog),
      registry_(CopyRegistry(frustHost.nodeLibraries())),
      palette_(registry_),
      graphView_(graph_, registry_),
      inspector_(graph_)
{
    browseTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    browseTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(browseTitle_);

    newNameEditor_.setTextToShowWhenEmpty("New behavior name...", juce::Colours::grey);
    addAndMakeVisible(newNameEditor_);

    newBehaviorButton_.onClick = [this] {
        const auto name = newNameEditor_.getText().trim();
        if (name.isEmpty()) return;
        catalog_.GetOrCreateGraph(name, frust::PodKind::Behavior); // registers an empty graph under this name.
        newNameEditor_.clear();
        RefreshBrowseList();
        OpenBehavior(name);
    };
    addAndMakeVisible(newBehaviorButton_);

    backButton_.onClick = [this] { ShowBrowseMode(); };
    addAndMakeVisible(backButton_);

    editTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    editTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(editTitle_);

    hint_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hint_);

    saveButton_.onClick = [this] { SaveGraph(); };
    saveButton_.setTooltip("Save this graph -- the graph is the model, this is what makes it reopenable.");
    addAndMakeVisible(saveButton_);

    compileButton_.onClick = [this] { CompileAndLoad(); };
    compileButton_.setTooltip("Generate FRust, write it as a cached loadable pod, and load it -- not just preview text.");
    addAndMakeVisible(compileButton_);

    status_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(status_);

    sourceView_.setMultiLine(true);
    sourceView_.setReadOnly(true);
    sourceView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    sourceView_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0e1218));
    sourceView_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd7e3f0));
    sourceView_.setText("Compile to see generated FRust and validation errors here.", juce::dontSendNotification);
    addAndMakeVisible(sourceView_);

    graphView_.onSelectionChanged = [this](node_system::NodeId id) { inspector_.SetSelectedNode(id); };
    graphView_.onGraphChanged = [this] { status_.setText("Graph edited", juce::dontSendNotification); };
    addChildComponent(palette_);
    addChildComponent(graphView_);
    addChildComponent(inspector_);

    RefreshBrowseList();
    ShowBrowseMode();
}

void FrustLogicPanel::RefreshBrowseList() {
    rows_.clear();
    auto names = catalog_.Names(frust::PodKind::Behavior);
    std::sort(names.begin(), names.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareIgnoreCase(b) < 0;
    });
    for (const auto& name : names) {
        auto* row = rows_.add(new BehaviorRow(*this, name));
        addAndMakeVisible(row);
    }
    resized();
}

void FrustLogicPanel::OpenBehavior(const juce::String& name) {
    // Graph holds unique_ptr<Node> internally -- it move-assigns, it does
    // not copy-assign. An independent editing copy (so Save doesn't alias
    // the catalog's stored graph while you're still mid-edit) goes through
    // the same .frgraph round-trip Save uses below, not a direct
    // assignment.
    node_system::Graph& stored = catalog_.GetOrCreateGraph(name, frust::PodKind::Behavior);
    std::string error;
    auto copy = node_system::DeserializeGraph(node_system::SerializeGraph(stored), error);
    if (!copy) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Cannot Open Behavior",
                                                "Could not load \"" + name + "\": " + juce::String(error));
        return;
    }
    graph_ = std::move(*copy);
    openName_ = name;
    editTitle_.setText("FRust Logic: " + name, juce::dontSendNotification);
    graphView_.GraphReplaced();
    sourceView_.setText("Compile to see generated FRust and validation errors here.", juce::dontSendNotification);
    status_.setText(juce::String(static_cast<int>(registry_.Types().size())) + " FRust nodes available", juce::dontSendNotification);
    ShowEditMode();
}

void FrustLogicPanel::ShowBrowseMode() {
    editing_ = false;
    RefreshBrowseList();
    browseTitle_.setVisible(true);
    newNameEditor_.setVisible(true);
    newBehaviorButton_.setVisible(true);
    for (auto* row : rows_) row->setVisible(true);

    backButton_.setVisible(false);
    editTitle_.setVisible(false);
    hint_.setVisible(false);
    saveButton_.setVisible(false);
    compileButton_.setVisible(false);
    status_.setVisible(false);
    sourceView_.setVisible(false);
    palette_.setVisible(false);
    graphView_.setVisible(false);
    inspector_.setVisible(false);
    resized();
}

void FrustLogicPanel::ShowEditMode() {
    editing_ = true;
    browseTitle_.setVisible(false);
    newNameEditor_.setVisible(false);
    newBehaviorButton_.setVisible(false);
    for (auto* row : rows_) row->setVisible(false);

    backButton_.setVisible(true);
    editTitle_.setVisible(true);
    hint_.setVisible(true);
    saveButton_.setVisible(true);
    compileButton_.setVisible(true);
    status_.setVisible(true);
    sourceView_.setVisible(true);
    palette_.setVisible(true);
    graphView_.setVisible(true);
    inspector_.setVisible(true);
    resized();
}

void FrustLogicPanel::SaveGraph() {
    if (openName_.isEmpty()) return;
    // Same round-trip as OpenBehavior, in reverse -- the catalog gets an
    // independent copy, so graph_ (and this editor) keeps working
    // unaffected by whatever the catalog does with its own copy afterward.
    std::string error;
    auto copy = node_system::DeserializeGraph(node_system::SerializeGraph(graph_), error);
    if (!copy) {
        status_.setText("Save failed: " + juce::String(error), juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }
    catalog_.GetOrCreateGraph(openName_, frust::PodKind::Behavior) = std::move(*copy);
    status_.setText("Saved \"" + openName_ + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void FrustLogicPanel::CompileAndLoad() {
    if (openName_.isEmpty()) return;

    node_system::FrustGraphCompileOptions options;
    // Every compiled Behavior exposes one hook today: on_tick. Other
    // lifecycle hooks (on_spawn, on_begin_play, ...) are real, documented
    // (FRUST_BEHAVIOR_LIFECYCLE.md) but not wired to authored graphs yet --
    // see docs/BEHAVIOR_COMPONENT_MODEL.md section 5.
    options.functionName = "on_tick";
    options.manifestJson = "{\"name\":\"" + openName_.toStdString() + "\",\"version\":\"0.1.0\"}";
    for (const auto& [id, library] : frustHost_.nodeLibraries().Libraries())
        options.sourceModules.insert(options.sourceModules.end(), library.frustSourceModules.begin(),
                                      library.frustSourceModules.end());

    // Auto-detect resultNode: first node with a Data output. Auto-detect
    // entryNode: first node with an Exec input that nothing else feeds --
    // the natural start of the exec chain. Both are the same "first found"
    // heuristic this panel already used for resultNode before control-flow
    // support existed; a graph with more than one candidate of either kind
    // needs an explicit picker, which is future UI, not a silent guess
    // beyond "first."
    for (const auto& [id, node] : graph_.Nodes()) {
        if (!node->Outputs().empty() && node->Outputs().front().type.kind == node_system::PinKind::Data) {
            options.resultNode = id;
            options.resultPin = node->Outputs().front().id;
            break;
        }
    }
    for (const auto& [id, node] : graph_.Nodes()) {
        const auto execInput = std::find_if(node->Inputs().begin(), node->Inputs().end(),
                                             [](const node_system::Pin& pin) { return pin.type.kind == node_system::PinKind::Exec; });
        if (execInput == node->Inputs().end()) continue;
        const bool hasIncoming = std::any_of(graph_.Connections().begin(), graph_.Connections().end(),
                                              [&](const node_system::Connection& c) { return c.toNode == id && c.toPin == execInput->id; });
        if (!hasIncoming) { options.entryNode = id; break; }
    }

    if (options.resultNode == 0 && options.entryNode == 0) {
        sourceView_.setText("Add at least one data-producing or executable node to compile this Behavior.",
                             juce::dontSendNotification);
        status_.setText("Nothing to compile", juce::dontSendNotification);
        return;
    }

    const auto result = node_system::CompileBehaviorGraphToFrust(graph_, frustHost_.nodeLibraries(), options);
    sourceView_.setText(juce::String(result.ok ? result.source : result.error), juce::dontSendNotification);
    if (!result.ok) {
        status_.setText("Compile failed", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }

    // The cached loadable pod, today, is the generated FRust source file
    // itself -- loaded through the same PluginRuntime::load() JIT path
    // loadBundled() already uses for EngineLifecycle.frust. This is not
    // yet the AOT-compiled native binary docs/BEHAVIOR_COMPONENT_MODEL.md
    // section 4 describes as the eventual ship artifact; that needs a
    // verified frust_compiler.exe CLI contract this pass didn't invest in
    // guessing at. What matters today -- the running behavior never
    // recompiles on every tick -- already holds: compiling happens once,
    // here, not during EngineFrustHost::tick().
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto behaviorsDir = executable.getParentDirectory().getChildFile("plugins").getChildFile("behaviors");
    if (!behaviorsDir.createDirectory().wasOk()) {
        status_.setText("Compiled, but could not create the behaviors cache directory", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }
    const auto cachedFile = behaviorsDir.getChildFile(openName_ + ".frust");
    if (!cachedFile.replaceWithText(result.source)) {
        status_.setText("Compiled, but could not write the cached pod file", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }

    std::string loadError;
    if (!frustHost_.loadObjectBehavior(openName_.toStdString(), cachedFile.getFullPathName().toStdString(), loadError)) {
        // Reloading an already-loaded pod isn't supported yet (see
        // BEHAVIOR_COMPONENT_MODEL.md section 7's hot-reload item) -- a
        // second Compile of the same Behavior is expected to fail here
        // until that's built, not a new bug.
        status_.setText("Compiled, but failed to load: " + juce::String(loadError), juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
        return;
    }

    catalog_.SetCompiledPodPath(openName_, cachedFile.getFullPathName());
    status_.setText("Compiled and loaded \"" + openName_ + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void FrustLogicPanel::resized()
{
    auto area = getLocalBounds().reduced(16, 12);

    if (!editing_) {
        auto header = area.removeFromTop(28);
        browseTitle_.setBounds(header);
        area.removeFromTop(8);
        auto newRow = area.removeFromTop(28);
        newBehaviorButton_.setBounds(newRow.removeFromRight(120).reduced(2));
        newRow.removeFromRight(8);
        newNameEditor_.setBounds(newRow);
        area.removeFromTop(8);
        for (auto* row : rows_) {
            row->setBounds(area.removeFromTop(26));
            area.removeFromTop(2);
        }
        return;
    }

    auto header = area.removeFromTop(48);
    auto headerTop = header.removeFromTop(24);
    backButton_.setBounds(headerTop.removeFromLeft(100));
    editTitle_.setBounds(headerTop);
    hint_.setBounds(header.removeFromTop(24));

    auto footer = area.removeFromBottom(150);
    auto footerHeader = footer.removeFromTop(26);
    saveButton_.setBounds(footerHeader.removeFromLeft(80));
    footerHeader.removeFromLeft(8);
    compileButton_.setBounds(footerHeader.removeFromLeft(100));
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
