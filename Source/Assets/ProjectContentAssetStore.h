#pragma once

#include <creation/assets/ProjectSession.h>

namespace creation::suite { struct SuiteSettings; }
namespace ce { struct LoadedModel; }

namespace ce::assets
{
// Writes source files into the active game's project content area and records
// their durable descriptor in the project's shared asset catalog.
class ProjectContentAssetStore final
{
public:
    // displayNameOverride/description are the Import Hub's first-import
    // metadata popup's Name/Description fields (Suite-Asset-Pipeline-
    // Model.md, Phase 3) -- empty (the default) preserves the original
    // behavior of always naming the asset after the source file with no
    // description, for callers that don't collect either (e.g. textures,
    // which skip the popup entirely).
    static bool importSource(creation::assets::ProjectSession& session,
                             const juce::String& gameAssetRoot,
                             const juce::File& sourceFile,
                             creation::assets::AssetKind kind,
                             const juce::String& category,
                             const juce::StringArray& tags,
                             creation::assets::AssetDescriptor& descriptor,
                             juce::String& errorMessage,
                             const juce::String& displayNameOverride = {},
                             const juce::String& description = {});

    // Materializes a managed project model only long enough for the Engine
    // decoder to parse it. The caller turns the returned CPU model into GPU
    // resources on the viewport's GL thread.
    static bool loadRenderable(const creation::assets::ProjectSession& session,
                               const creation::suite::SuiteSettings& settings,
                               const juce::String& assetId,
                               const juce::String& versionId,
                               ce::LoadedModel& model,
                               creation::assets::MaterializedAssetLease& lease,
                               juce::String& errorMessage);
};
} // namespace ce::assets
