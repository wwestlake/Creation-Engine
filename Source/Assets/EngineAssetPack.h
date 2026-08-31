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
    static constexpr const char* version = "1.0.1";

    static bool ensureInstalled(juce::String& errorMessage);
    static bool readDefaultScene(juce::MemoryBlock& sceneData, juce::String& errorMessage);

    [[nodiscard]] static juce::String vfsRoot();
    [[nodiscard]] static juce::String manifestPath();
    [[nodiscard]] static juce::String defaultScenePath();
};
} // namespace ce::assets
