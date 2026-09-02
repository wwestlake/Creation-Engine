#pragma once

#include <unordered_map>
#include <vector>

#include <JuceHeader.h>
#include <node_system/graph.h>

namespace ce::frust {

// Named registry of Behavior graphs -- the graph is the saved model, same
// split Material::savedGraphSource/compiledMaterialSource already
// established (see docs/BEHAVIOR_COMPONENT_MODEL.md section 4). Runtime-
// only for now, same scope Materials themselves currently have (not yet
// tracked through ProjectSession) -- see that doc's open asset-tracking-
// depth question.
struct BehaviorEntry {
    node_system::Graph graph;

    // Absolute path to the cached compiled artifact this graph last
    // compiled to -- a .frust source file today, loaded through the same
    // PluginRuntime::load() JIT path loadBundled() already uses for
    // EngineLifecycle.frust, NOT yet a separately AOT-compiled native pod
    // via frust_compiler.exe (see BEHAVIOR_COMPONENT_MODEL.md section 4's
    // note on this being deferred, not assumed done). Empty until Compile
    // succeeds at least once.
    juce::String compiledPodPath;
};

class BehaviorCatalog final {
public:
    // Returns the SAME Graph for repeated calls with the same name (never
    // rebuilds it) -- same convention AssetCatalog::GetOrCreateMaterial
    // already uses. Creates an empty Behavior-target graph if new.
    node_system::Graph& GetOrCreate(const juce::String& name);
    [[nodiscard]] node_system::Graph* Find(const juce::String& name);
    [[nodiscard]] std::vector<juce::String> Names() const;
    bool Remove(const juce::String& name);

    void SetCompiledPodPath(const juce::String& name, const juce::String& path);
    [[nodiscard]] juce::String CompiledPodPath(const juce::String& name) const;

private:
    std::unordered_map<std::string, BehaviorEntry> entries_;
};

} // namespace ce::frust
