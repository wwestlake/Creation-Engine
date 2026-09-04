#include "Scene/ObjectDefinitionNaming.h"

#include <algorithm>

namespace ce::scene {

juce::String GenerateWrapperDefinitionName(const ObjectDefinitionCatalog& catalog, const juce::String& baseName)
{
    const auto existing = catalog.ids();
    auto isTaken = [&](const juce::String& candidate) {
        return std::any_of(existing.begin(), existing.end(),
                            [&](const juce::String& id) { return id.equalsIgnoreCase(candidate); });
    };
    if (!isTaken(baseName)) return baseName;
    for (int i = 2; i < 1000; ++i) {
        const auto candidate = baseName + " " + juce::String(i);
        if (!isTaken(candidate)) return candidate;
    }
    return baseName + " " + juce::String(juce::Time::currentTimeMillis());
}

juce::String FindWrapperDefinitionForRenderAsset(const ObjectDefinitionCatalog& catalog, const juce::String& meshAssetId)
{
    for (const auto& id : catalog.ids()) {
        const auto* definition = catalog.find(id);
        if (definition == nullptr || definition->components.empty()) continue;
        const bool allMatch =
            std::all_of(definition->components.begin(), definition->components.end(), [&](const ObjectComponentEntry& component) {
                return component.kind == ObjectComponentKind::Mesh && component.meshAssetId == meshAssetId;
            });
        if (allMatch) return id;
    }
    return {};
}

} // namespace ce::scene
