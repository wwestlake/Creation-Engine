#pragma once

#include "Assets/AssetPackStore.h"

namespace ce::assets
{
// Authored Engine Pack content is installed into the suite VFS on first use.
// Games subsequently read only the VFS copy, never a machine-local asset path.
class EngineAssetPack final
{
public:
    static constexpr const char* packId = "com.lagdaemon.creation-engine";
    static constexpr const char* version = "1.0.3";

    static bool ensureInstalled(juce::String& errorMessage);
    static bool readDefaultScene(juce::MemoryBlock& sceneData, juce::String& errorMessage);

    // Reads any authored starter scene by id (e.g. "DefaultScene",
    // "01_Welcome_Yard") -- readDefaultScene is a thin wrapper over this
    // for "DefaultScene" specifically, kept so existing callers don't need
    // to change.
    static bool readScene(const juce::String& templateSceneId, juce::MemoryBlock& sceneData, juce::String& errorMessage);

    [[nodiscard]] static juce::String vfsRoot();
    [[nodiscard]] static juce::String manifestPath();
    [[nodiscard]] static juce::String defaultScenePath();
};
} // namespace ce::assets
