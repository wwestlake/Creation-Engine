#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>
#include <creation/assets/AssetTypes.h>
#include <node_system/graph.h>

namespace creation::assets { class ProjectSession; }

namespace ce::frust {

// What domain a Pod affects and what its inputs mean -- an invocation-
// contract question, not a content-domain label. Behavior Pods are
// invoked by EngineFrustHost's entity lifecycle; Processing Pods are
// invoked on demand. See docs/architecture/ (Pod Management System plan)
// for the full three-axis model this is one axis of.
enum class PodKind { Behavior, Processing };

// How the Pod's model is authored: a NodeSystem graph on the canvas, or
// hand-typed FRust source text. Both feed the same save/compile/reflect
// pipeline -- Source Pods (PodAuthoringMode::Source) are a later phase,
// this enum exists now so PodEntry has a real field for it from the
// start rather than retrofitting one in.
enum class PodAuthoringMode { Graph, Source };

// Named registry of Pods -- generalizes the Phase 4.5 BehaviorCatalog to
// cover both Kinds and both authoring modes, with real ProjectSession
// persistence (BehaviorCatalog was runtime-only, never saved). The graph
// (or source text) IS the saved model, same split
// Material::savedGraphSource/compiledMaterialSource already established.
struct PodEntry {
    PodKind kind = PodKind::Behavior;
    PodAuthoringMode authoringMode = PodAuthoringMode::Graph;
    // node_system::Graph has no default constructor (it always needs a
    // name), so this needs a default member initializer or PodEntry's
    // own default constructor is implicitly deleted.
    node_system::Graph graph { "untitled", node_system::GraphTarget::Behavior };
    juce::String sourceText;

    // Absolute path to the cached compiled artifact this Pod last
    // compiled to -- same semantics as BehaviorEntry::compiledPodPath.
    // Empty until Compile succeeds at least once.
    juce::String compiledPodPath;

    // Empty until the first Save persists this Pod -- a second Save
    // reuses this id so ProjectAssetService::saveGeneratedAsset bumps
    // revision instead of minting a new asset.
    creation::assets::AssetId assetId;

    // Orthogonal capability flag (not tied to Kind): whether this Pod's
    // compiled output should also self-reflect as a draggable node type
    // in other graphs. Wired up starting Phase 5 of the Pod plan; the
    // field lives here from the start so persistence round-trips it.
    bool exposeAsNode = false;
};

class PodCatalog final {
public:
    // Returns the SAME Graph for repeated calls with the same name
    // (never rebuilds it) -- same convention BehaviorCatalog::GetOrCreate
    // and AssetCatalog::GetOrCreateMaterial already use. Creates a new
    // entry (graph-authored, target set from kind) if new; if an entry
    // with this name already exists as a Source Pod, its authoring mode
    // is left unchanged -- this only ever returns/creates the graph
    // side.
    node_system::Graph& GetOrCreateGraph(const juce::String& name, PodKind kind);

    // Same idea for Source Pods -- returns the same sourceText string
    // for repeated calls, creating a new Source-authored entry if new.
    juce::String& GetOrCreateSource(const juce::String& name, PodKind kind);

    [[nodiscard]] node_system::Graph* FindGraph(const juce::String& name);
    [[nodiscard]] std::vector<juce::String> Names(std::optional<PodKind> filterKind = std::nullopt) const;
    bool Remove(const juce::String& name);

    [[nodiscard]] bool Contains(const juce::String& name) const;
    [[nodiscard]] PodKind Kind(const juce::String& name) const;
    [[nodiscard]] PodAuthoringMode AuthoringMode(const juce::String& name) const;

    void SetCompiledPodPath(const juce::String& name, const juce::String& path);
    [[nodiscard]] juce::String CompiledPodPath(const juce::String& name) const;

    void SetExposeAsNode(const juce::String& name, bool expose);
    [[nodiscard]] bool ExposeAsNode(const juce::String& name) const;

    // Persists one Pod's current model (graph or source, per its
    // authoring mode) as an AssetKind::pod asset. kind/authoringMode are
    // embedded in the saved payload itself (self-describing), not only
    // recorded here -- so the Pod stays correctly identified wherever it
    // travels, independent of which catalog record currently points at
    // it. A second Save reuses the entry's stored assetId, bumping
    // revision rather than minting a new asset.
    bool Save(creation::assets::ProjectSession& session, const juce::String& name, juce::String& error);

    // Queries every AssetKind::pod asset in the project, reads each
    // one's payload, parses the embedded kind/authoringMode metadata,
    // and populates entries_ from it -- the inverse of Save. Intended to
    // run once when a project is opened. Existing in-memory entries not
    // backed by a persisted asset are left untouched.
    bool LoadAll(creation::assets::ProjectSession& session, juce::String& error);

private:
    PodEntry& GetOrCreateEntry(const juce::String& name, PodKind kind);

    std::unordered_map<std::string, PodEntry> entries_;
};

} // namespace ce::frust
