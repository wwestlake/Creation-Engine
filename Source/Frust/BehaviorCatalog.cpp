#include "Frust/BehaviorCatalog.h"

namespace ce::frust {

node_system::Graph& BehaviorCatalog::GetOrCreate(const juce::String& name) {
    const auto key = name.toStdString();
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        it = entries_.emplace(key, BehaviorEntry{ node_system::Graph(key, node_system::GraphTarget::Behavior), {} }).first;
    }
    return it->second.graph;
}

node_system::Graph* BehaviorCatalog::Find(const juce::String& name) {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? nullptr : &it->second.graph;
}

std::vector<juce::String> BehaviorCatalog::Names() const {
    std::vector<juce::String> result;
    result.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) result.push_back(name);
    return result;
}

bool BehaviorCatalog::Remove(const juce::String& name) {
    return entries_.erase(name.toStdString()) != 0;
}

void BehaviorCatalog::SetCompiledPodPath(const juce::String& name, const juce::String& path) {
    const auto it = entries_.find(name.toStdString());
    if (it != entries_.end()) it->second.compiledPodPath = path;
}

juce::String BehaviorCatalog::CompiledPodPath(const juce::String& name) const {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? juce::String() : it->second.compiledPodPath;
}

} // namespace ce::frust
