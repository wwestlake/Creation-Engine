#include "PodEditorPanel.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace ce::views
{

namespace
{
node_system::NodeTypeRegistry CopyRegistry(const node_system::NodeLibraryRegistry& libraries)
{
    node_system::NodeTypeRegistry result;
    // Control-flow nodes (branch/sequence/for/while/break/continue/return)
    // are native to NodeSystem itself, not reflected from any FRust
    // plugin -- registerNodeLibraries() never sees them, so they have to
    // be added here explicitly or the palette never offers them at all,
    // even though frust_codegen.cpp has real lowering for every one.
    node_system::RegisterCoreControlFlowNodes(result);
    // Event nodes (On Tick/On Begin Play/On End Play) -- same reasoning:
    // structural, not FRust-reflected. See CompileAndLoad() below for
    // where these actually drive multi-entry compilation.
    node_system::RegisterCoreEventNodes(result);
    // Entity-self accessor + Bool/Int/String Variable Get/Set -- same
    // reasoning, plus these are host-extern calls (see
    // NodeTypeDescriptor::isHostExtern).
    node_system::RegisterCoreVariableNodes(result);
    // Asset Exists -- the first real capability node (Phase 8), gated by
    // "engine.asset.query" (documentation of intent; see
    // core_control_flow.h's note on why enforcement doesn't apply to
    // this first-party registration path).
    node_system::RegisterCoreCapabilityNodes(result);
    for (const auto& [name, descriptor] : libraries.TypeRegistry().Types())
        result.Register(descriptor);
    return result;
}

juce::String KindLabel(frust::PodKind kind) { return kind == frust::PodKind::Processing ? "Processing" : "Behavior"; }

// Kind-only creation (Phase 6) has no name box to fill in, so a default
// name is generated instead -- "New Behavior Pod", then "New Behavior
// Pod 2", etc., the same "first free numbered slot" shape most editors
// use for untitled documents.
juce::String GenerateDefaultPodName(frust::PodCatalog& catalog, frust::PodKind kind) {
    const juce::String base = "New " + KindLabel(kind) + " Pod";
    const auto existing = catalog.Names(kind);
    auto isTaken = [&](const juce::String& candidate) {
        return std::any_of(existing.begin(), existing.end(),
                            [&](const juce::String& name) { return name.equalsIgnoreCase(candidate); });
    };
    if (!isTaken(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        const auto candidate = base + " " + juce::String(i);
        if (!isTaken(candidate)) return candidate;
    }
    return base + " " + juce::String(juce::Time::currentTimeMillis());
}

// Syntax highlighting for hand-typed Source Pods (Phase 8). Reuses
// JUCE's generic C-like lexer (CppTokeniserFunctions) for comments,
// strings, numbers, operators and brackets -- FRust's surface syntax is
// C-like enough that this is genuinely correct, not an approximation.
// The one thing it can't do on its own is recognize FRust's own
// keywords (they're not in C++'s reserved-word table), so identifiers
// it doesn't classify as a C++ keyword are re-checked against FRust's
// real keyword set (observed directly in this repo's .frust files --
// EngineLifecycle.frust, CoreNodes.frust/CoreNodesLibrary.frust -- plus
// the control-flow/type keywords FrustLang's own codegen supports).
bool IsFrustKeyword(const juce::String& token) {
    static const juce::StringArray kKeywords {
        "manifest", "use", "self", "extern", "pub", "fn", "node", "pure",
        "callable", "loop", "let", "if", "else", "while", "for", "return",
        "break", "continue", "true", "false",
        "i32", "i64", "f32", "f64", "bool", "String", "usize"
    };
    return kKeywords.contains(token);
}

class FrustCodeTokeniser final : public juce::CodeTokeniser {
public:
    int readNextToken(juce::CodeDocument::Iterator& source) override {
        auto start = source;
        const int tokenType = juce::CppTokeniserFunctions::readNextToken(source);
        if (tokenType == juce::CPlusPlusCodeTokeniser::tokenType_identifier) {
            juce::String token;
            while (start.getPosition() < source.getPosition()) token << start.nextChar();
            if (IsFrustKeyword(token)) return juce::CPlusPlusCodeTokeniser::tokenType_keyword;
        }
        return tokenType;
    }

    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override {
        return juce::CPlusPlusCodeTokeniser().getDefaultColourScheme();
    }
};

// Per-Kind starting content for a new Source Pod -- the same idea as a
// Graph Pod simply starting empty, just with real starter text since a
// blank source file gives an author nothing to go on the way a blank
// canvas at least offers a palette to drag from.
juce::String SourceTemplateFor(frust::PodKind kind) {
    if (kind == frust::PodKind::Processing) {
        return "// Processing Pod -- invoked on demand, not entity-scoped.\n"
               "// Check \"Expose as node\" is not needed here: a Source Pod\n"
               "// reflects as a node automatically if you prefix a function\n"
               "// with `node pure` or `node callable` yourself, same as any\n"
               "// other FRust node declaration.\n"
               "node pure pub fn process(value: i64) -> i64 = {\n"
               "    value\n"
               "}\n";
    }
    return "// Behavior Pod -- invoked per-entity by EngineFrustHost's lifecycle.\n"
           "pub fn on_tick() -> i64 = {\n"
           "    0\n"
           "}\n";
}

struct Snippet { const char* label; const char* text; };
constexpr Snippet kSnippets[] = {
    { "node pure fn", "node pure pub fn name(value: i64) -> i64 = {\n    value\n}\n" },
    { "node callable fn", "node callable pub fn name() -> i64 = {\n    1\n}\n" },
    { "if / else", "if condition {\n    \n} else {\n    \n}\n" },
    { "while loop", "while condition {\n    \n}\n" },
    { "for loop", "for i in 0..10 {\n    \n}\n" },
    { "let binding", "let name = value;\n" },
};
}

// One row in Browse mode: a Pod's name plus Open. Deliberately no
// Delete here yet -- see docs/BEHAVIOR_COMPONENT_MODEL.md's open asset-
// tracking-depth question; a real delete needs the same dependency-check
// treatment ContentBrowserPanel gives project assets, which Pods aren't
// tracked through the full pipeline as yet.
class PodEditorPanel::PodRow final : public juce::Component {
public:
    PodRow(PodEditorPanel& owner, juce::String name) : owner_(owner), name_(std::move(name)) {
        nameLabel_.setText(name_, juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        openButton_.onClick = [this] { owner_.OpenPod(name_); };
        addAndMakeVisible(openButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        openButton_.setBounds(bounds.removeFromRight(70).reduced(2));
        nameLabel_.setBounds(bounds);
    }

private:
    PodEditorPanel& owner_;
    juce::String name_;
    juce::Label nameLabel_;
    juce::TextButton openButton_ { "Open" };
};

PodEditorPanel::~PodEditorPanel() = default;

PodEditorPanel::PodEditorPanel(frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog,
                               creation::assets::ProjectSession& projectSession)
    : frustHost_(frustHost),
      catalog_(catalog),
      projectSession_(projectSession),
      registry_(CopyRegistry(frustHost.nodeLibraries())),
      palette_(registry_),
      graphView_(graph_, registry_)
{
    browseTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    browseTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(browseTitle_);

    newBehaviorPodButton_.onClick = [this] { CreatePod(frust::PodKind::Behavior); };
    addAndMakeVisible(newBehaviorPodButton_);
    newProcessingPodButton_.onClick = [this] { CreatePod(frust::PodKind::Processing); };
    addAndMakeVisible(newProcessingPodButton_);
    newAuthoringModeCombo_.addItemList(juce::StringArray { "Graph", "Source" }, 1);
    newAuthoringModeCombo_.setSelectedItemIndex(0, juce::dontSendNotification);
    newAuthoringModeCombo_.setTooltip("Graph: node canvas. Source: hand-typed FRust.");
    addAndMakeVisible(newAuthoringModeCombo_);

    behaviorSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    behaviorSectionLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    addAndMakeVisible(behaviorSectionLabel_);
    processingSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    processingSectionLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    addAndMakeVisible(processingSectionLabel_);

    backButton_.onClick = [this] { ShowBrowseMode(); };
    addAndMakeVisible(backButton_);

    editTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    editTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(editTitle_);

    hint_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hint_);

    saveButton_.onClick = [this] { SaveContent(); };
    saveButton_.setTooltip("Save this Pod to the project -- the graph is the model, this is what makes it reopenable.");
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

    graphView_.onSelectionChanged = [this](node_system::NodeId id) {
        if (onSelectedNodeChanged) onSelectedNodeChanged(id);
    };
    graphView_.onGraphChanged = [this] { status_.setText("Graph edited", juce::dontSendNotification); };
    addChildComponent(palette_);
    addChildComponent(graphView_);

    frustTokeniser_ = std::make_unique<FrustCodeTokeniser>();
    sourceEditor_ = std::make_unique<juce::CodeEditorComponent>(sourceCodeDocument_, frustTokeniser_.get());
    sourceEditor_->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
    sourceEditor_->setColourScheme(frustTokeniser_->getDefaultColourScheme());
    addChildComponent(*sourceEditor_);

    for (const auto& snippet : kSnippets) snippetCombo_.addItem(snippet.label, snippetCombo_.getNumItems() + 1);
    snippetCombo_.setTextWhenNothingSelected("Snippet...");
    addChildComponent(snippetCombo_);
    insertSnippetButton_.onClick = [this] { InsertSnippet(); };
    addChildComponent(insertSnippetButton_);

    RefreshBrowseList();
    ShowBrowseMode();
}

void PodEditorPanel::RefreshBrowseList() {
    behaviorRows_.clear();
    processingRows_.clear();

    auto behaviorNames = catalog_.Names(frust::PodKind::Behavior);
    auto processingNames = catalog_.Names(frust::PodKind::Processing);
    auto sortNames = [](std::vector<juce::String>& names) {
        std::sort(names.begin(), names.end(), [](const juce::String& a, const juce::String& b) {
            return a.compareIgnoreCase(b) < 0;
        });
    };
    sortNames(behaviorNames);
    sortNames(processingNames);

    for (const auto& name : behaviorNames) {
        auto* row = behaviorRows_.add(new PodRow(*this, name));
        addAndMakeVisible(row);
    }
    for (const auto& name : processingNames) {
        auto* row = processingRows_.add(new PodRow(*this, name));
        addAndMakeVisible(row);
    }
    resized();
}

void PodEditorPanel::CreatePod(frust::PodKind kind) {
    const auto name = GenerateDefaultPodName(catalog_, kind);
    if (newAuthoringModeCombo_.getSelectedItemIndex() == 1) {
        auto& source = catalog_.GetOrCreateSource(name, kind);
        source = SourceTemplateFor(kind);
    } else {
        catalog_.GetOrCreateGraph(name, kind); // registers an empty graph under this name.
    }
    RefreshBrowseList();
    OpenPod(name);
}

void PodEditorPanel::OpenPod(const juce::String& name) {
    const auto kind = catalog_.Kind(name);
    const auto mode = catalog_.AuthoringMode(name);
    openName_ = name;
    editTitle_.setText(KindLabel(kind) + " Pod: " + name, juce::dontSendNotification);
    sourceView_.setText("Compile to see generated FRust and validation errors here.", juce::dontSendNotification);

    if (mode == frust::PodAuthoringMode::Source) {
        auto* entry = catalog_.FindEntry(name);
        sourceCodeDocument_.replaceAllContent(entry ? entry->sourceText : juce::String());
        status_.setText("Editing FRust source directly", juce::dontSendNotification);
    } else {
        // Graph holds unique_ptr<Node> internally -- it move-assigns, it
        // does not copy-assign. An independent editing copy (so Save
        // doesn't alias the catalog's stored graph while you're still
        // mid-edit) goes through the same .frgraph round-trip Save uses,
        // not a direct assignment.
        node_system::Graph& stored = catalog_.GetOrCreateGraph(name, kind);
        std::string error;
        auto copy = node_system::DeserializeGraph(node_system::SerializeGraph(stored), error);
        if (!copy) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Cannot Open Pod",
                                                    "Could not load \"" + name + "\": " + juce::String(error));
            return;
        }
        graph_ = std::move(*copy);
        graphView_.GraphReplaced();
        status_.setText(juce::String(static_cast<int>(registry_.Types().size())) + " FRust nodes available", juce::dontSendNotification);
    }
    ShowEditMode();
    if (onOpenPodChanged) onOpenPodChanged(openName_);
    if (onSelectedNodeChanged) onSelectedNodeChanged(0);
}

void PodEditorPanel::ShowBrowseMode() {
    editing_ = false;
    RefreshBrowseList();
    browseTitle_.setVisible(true);
    newBehaviorPodButton_.setVisible(true);
    newProcessingPodButton_.setVisible(true);
    newAuthoringModeCombo_.setVisible(true);
    behaviorSectionLabel_.setVisible(true);
    processingSectionLabel_.setVisible(true);
    for (auto* row : behaviorRows_) row->setVisible(true);
    for (auto* row : processingRows_) row->setVisible(true);

    backButton_.setVisible(false);
    editTitle_.setVisible(false);
    hint_.setVisible(false);
    saveButton_.setVisible(false);
    compileButton_.setVisible(false);
    status_.setVisible(false);
    sourceView_.setVisible(false);
    palette_.setVisible(false);
    graphView_.setVisible(false);
    sourceEditor_->setVisible(false);
    snippetCombo_.setVisible(false);
    insertSnippetButton_.setVisible(false);
    resized();

    openName_.clear();
    if (onOpenPodChanged) onOpenPodChanged(openName_);
    if (onSelectedNodeChanged) onSelectedNodeChanged(0);
}

void PodEditorPanel::ShowEditMode() {
    editing_ = true;
    browseTitle_.setVisible(false);
    newBehaviorPodButton_.setVisible(false);
    newProcessingPodButton_.setVisible(false);
    newAuthoringModeCombo_.setVisible(false);
    behaviorSectionLabel_.setVisible(false);
    processingSectionLabel_.setVisible(false);
    for (auto* row : behaviorRows_) row->setVisible(false);
    for (auto* row : processingRows_) row->setVisible(false);

    const bool isSource = catalog_.AuthoringMode(openName_) == frust::PodAuthoringMode::Source;

    backButton_.setVisible(true);
    editTitle_.setVisible(true);
    hint_.setVisible(true);
    saveButton_.setVisible(true);
    compileButton_.setVisible(true);
    status_.setVisible(true);
    sourceView_.setVisible(true);
    palette_.setVisible(!isSource);
    graphView_.setVisible(!isSource);
    sourceEditor_->setVisible(isSource);
    snippetCombo_.setVisible(isSource);
    insertSnippetButton_.setVisible(isSource);
    resized();
}

void PodEditorPanel::SaveContent() {
    if (openName_.isEmpty()) return;
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr) return;

    if (entry->authoringMode == frust::PodAuthoringMode::Source) {
        entry->sourceText = sourceCodeDocument_.getAllContent();
    } else {
        // Same round-trip as OpenPod, in reverse -- the catalog gets an
        // independent copy, so graph_ (and this editor) keeps working
        // unaffected by whatever the catalog does with its own copy
        // afterward.
        std::string error;
        auto copy = node_system::DeserializeGraph(node_system::SerializeGraph(graph_), error);
        if (!copy) {
            status_.setText("Save failed: " + juce::String(error), juce::dontSendNotification);
            status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return;
        }
        entry->graph = std::move(*copy);
    }

    juce::String persistError;
    if (!catalog_.Save(projectSession_, openName_, persistError)) {
        status_.setText("Saved locally, but could not persist to the project: " + persistError, juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
        return;
    }
    status_.setText("Saved \"" + openName_ + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void PodEditorPanel::CompileAndLoad() {
    if (openName_.isEmpty()) return;
    auto* entry = catalog_.FindEntry(openName_);
    if (entry == nullptr) return;

    std::string frustSource;

    if (entry->authoringMode == frust::PodAuthoringMode::Source) {
        // A Source Pod's model IS its FRust text directly -- no
        // graph-to-text step, no manifest/exposeAsNode injection (the
        // author writes `manifest "...";` and `node pure`/`node
        // callable` themselves, same as EngineLifecycle.frust and
        // CoreNodes.frust do). Invalid syntax surfaces exactly the same
        // way it would for a Graph Pod: at the load step below, via the
        // JIT compiler's own error text -- never blocks typing.
        frustSource = sourceCodeDocument_.getAllContent().toStdString();
        sourceView_.setText(sourceCodeDocument_.getAllContent(), juce::dontSendNotification);
        if (juce::String(frustSource).trim().isEmpty()) {
            status_.setText("Nothing to compile", juce::dontSendNotification);
            return;
        }
    } else {

    // Base options shared by every function this Pod compiles to --
    // per-function fields (functionName/entryNode/resultNode/
    // emitManifestAndImports) are set per compile call below.
    node_system::FrustGraphCompileOptions baseOptions;
    baseOptions.manifestJson = "{\"name\":\"" + openName_.toStdString() + "\",\"version\":\"0.1.0\"}";
    baseOptions.exposeAsNode = catalog_.ExposeAsNode(openName_);
    for (const auto& [id, library] : frustHost_.nodeLibraries().Libraries())
        baseOptions.sourceModules.insert(baseOptions.sourceModules.end(), library.frustSourceModules.begin(),
                                          library.frustSourceModules.end());
    // Explicit interface (Phase 6) applies to every function compiled
    // below, event-driven or not -- it declares this Pod's parameters,
    // not any one function's.
    for (const auto& input : entry->interfaceInputs) {
        baseOptions.parameters.push_back({ input.name.toStdString(), input.type });
        if (input.boundNode != 0)
            baseOptions.inputBindings.push_back({ input.boundNode, input.boundPin, input.name.toStdString() });
    }

    // Event nodes (On Tick/On Begin Play/On End Play, Phase 4) give
    // execution an explicit, visible start -- each one found compiles to
    // its own real lifecycle-hook function, concatenated into one .frust
    // file (first compile emits the shared manifest/imports header, the
    // rest skip it). Falls back to the pre-Phase-4 single-function
    // behavior below when the graph has no Event node at all, so every
    // Pod created before this phase keeps compiling exactly as it did.
    std::vector<node_system::NodeId> eventNodes;
    for (const auto& [id, node] : graph_.Nodes())
        if (!node_system::EventNodeFrustFunctionName(node->TypeName()).empty()) eventNodes.push_back(id);

    std::ostringstream combinedSource;
    if (!eventNodes.empty()) {
        // Each function compiles with its own manifest/imports suppressed
        // (emitManifestAndImports = false for all of them, not just all-
        // but-the-first) -- extern declarations returned by different
        // compiles need deduping ACROSS every call before any of them are
        // emitted, which isn't possible if one call already baked its own
        // copy into `source`. The shared header (manifest + imports +
        // deduped externs) is assembled here instead, once, up front.
        std::map<std::string, std::string> externsByName;
        std::vector<std::string> functionBodies;
        for (size_t i = 0; i < eventNodes.size(); ++i) {
            const auto* node = graph_.FindNode(eventNodes[i]);
            auto options = baseOptions;
            options.functionName = node_system::EventNodeFrustFunctionName(node->TypeName());
            options.entryNode = eventNodes[i];
            options.emitManifestAndImports = false;

            const auto result = node_system::CompileBehaviorGraphToFrust(graph_, frustHost_.nodeLibraries(), options);
            if (!result.ok) {
                sourceView_.setText(juce::String(result.error), juce::dontSendNotification);
                status_.setText("Compile failed (" + juce::String(options.functionName) + ")", juce::dontSendNotification);
                status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
                return;
            }
            functionBodies.push_back(result.source);
            for (const auto& decl : result.externDeclarations) externsByName[decl] = decl;
        }

        if (!baseOptions.manifestJson.empty())
            combinedSource << "manifest \"" << node_system::EscapeFrustString(baseOptions.manifestJson) << "\";\n\n";
        std::set<std::string> uniqueModules(baseOptions.sourceModules.begin(), baseOptions.sourceModules.end());
        for (const auto& module : uniqueModules) combinedSource << "use self::" << module << ";\n";
        if (!uniqueModules.empty()) combinedSource << "\n";
        for (const auto& [name, decl] : externsByName) { (void)name; combinedSource << decl; }
        if (!externsByName.empty()) combinedSource << "\n";
        for (const auto& body : functionBodies) combinedSource << body << "\n";
    } else {
        auto options = baseOptions;
        // Every compiled Behavior exposes one hook today: on_tick. Other
        // lifecycle hooks (on_spawn, on_begin_play, ...) are real,
        // documented (FRUST_BEHAVIOR_LIFECYCLE.md) but only reachable
        // from a graph via an explicit Event node (above) -- this
        // fallback path predates that and only ever produces on_tick.
        options.functionName = "on_tick";

        const bool hasExplicitInterface = !entry->interfaceInputs.empty() || entry->outputNode != 0;
        if (hasExplicitInterface) {
            options.resultNode = entry->outputNode;
            options.resultPin = entry->outputPin;
        } else {
            // Auto-detect resultNode: first node with a Data output.
            // Auto-detect entryNode: first node with an Exec input that
            // nothing else feeds -- the natural start of the exec chain.
            // Both are the "first found" heuristic this panel used
            // before Phase 6's explicit interface editor existed.
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
        }

        if (options.resultNode == 0 && options.entryNode == 0) {
            sourceView_.setText("Add at least one data-producing or executable node to compile this Pod.",
                                 juce::dontSendNotification);
            status_.setText("Nothing to compile", juce::dontSendNotification);
            return;
        }

        // Same reasoning as the Event-node path above: build the header
        // ourselves so a host-extern node used outside any Event chain
        // (e.g. a Processing Pod reading a Variable) still gets its
        // extern fn declaration emitted -- the compiler itself never
        // embeds these in `source` anymore, only returns them as data.
        options.emitManifestAndImports = false;
        const auto result = node_system::CompileBehaviorGraphToFrust(graph_, frustHost_.nodeLibraries(), options);
        if (!result.ok) {
            sourceView_.setText(juce::String(result.error), juce::dontSendNotification);
            status_.setText("Compile failed", juce::dontSendNotification);
            status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return;
        }
        if (!options.manifestJson.empty())
            combinedSource << "manifest \"" << node_system::EscapeFrustString(options.manifestJson) << "\";\n\n";
        std::set<std::string> uniqueModules(options.sourceModules.begin(), options.sourceModules.end());
        for (const auto& module : uniqueModules) combinedSource << "use self::" << module << ";\n";
        if (!uniqueModules.empty()) combinedSource << "\n";
        for (const auto& decl : result.externDeclarations) combinedSource << decl;
        if (!result.externDeclarations.empty()) combinedSource << "\n";
        combinedSource << result.source;
    }

    frustSource = combinedSource.str();
    sourceView_.setText(juce::String(frustSource), juce::dontSendNotification);
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
    if (!cachedFile.replaceWithText(frustSource)) {
        status_.setText("Compiled, but could not write the cached pod file", juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }

    std::string loadError;
    if (!frustHost_.loadObjectBehavior(openName_.toStdString(), cachedFile.getFullPathName().toStdString(), loadError)) {
        // Reloading an already-loaded pod isn't supported yet (see
        // BEHAVIOR_COMPONENT_MODEL.md section 7's hot-reload item) -- a
        // second Compile of the same Pod is expected to fail here until
        // that's built, not a new bug.
        status_.setText("Compiled, but failed to load: " + juce::String(loadError), juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
        return;
    }

    catalog_.SetCompiledPodPath(openName_, cachedFile.getFullPathName());
    status_.setText("Compiled and loaded \"" + openName_ + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void PodEditorPanel::InsertSnippet() {
    const int index = snippetCombo_.getSelectedItemIndex();
    if (index < 0 || static_cast<size_t>(index) >= std::size(kSnippets)) return;
    sourceEditor_->insertTextAtCaret(kSnippets[static_cast<size_t>(index)].text);
    snippetCombo_.setSelectedItemIndex(-1, juce::dontSendNotification);
    sourceEditor_->grabKeyboardFocus();
}

void PodEditorPanel::resized()
{
    auto area = getLocalBounds().reduced(16, 12);

    if (!editing_) {
        auto header = area.removeFromTop(28);
        browseTitle_.setBounds(header);
        area.removeFromTop(8);
        auto newRow = area.removeFromTop(28);
        newAuthoringModeCombo_.setBounds(newRow.removeFromRight(80).reduced(1));
        newRow.removeFromRight(8);
        newProcessingPodButton_.setBounds(newRow.removeFromRight(150).reduced(2));
        newRow.removeFromRight(4);
        newBehaviorPodButton_.setBounds(newRow.removeFromRight(140).reduced(2));
        area.removeFromTop(12);

        behaviorSectionLabel_.setBounds(area.removeFromTop(20));
        for (auto* row : behaviorRows_) {
            row->setBounds(area.removeFromTop(26));
            area.removeFromTop(2);
        }
        area.removeFromTop(10);
        processingSectionLabel_.setBounds(area.removeFromTop(20));
        for (auto* row : processingRows_) {
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

    if (catalog_.AuthoringMode(openName_) == frust::PodAuthoringMode::Source) {
        auto snippetRow = area.removeFromTop(26);
        insertSnippetButton_.setBounds(snippetRow.removeFromRight(60).reduced(1));
        snippetRow.removeFromRight(4);
        snippetCombo_.setBounds(snippetRow.removeFromRight(160).reduced(1));
        area.removeFromTop(4);
        sourceEditor_->setBounds(area);
        return;
    }

    auto left = area.removeFromLeft(190);
    palette_.setBounds(left);
    area.removeFromLeft(8);
    graphView_.setBounds(area);
}

void PodEditorPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10141a));
}

} // namespace ce::views
