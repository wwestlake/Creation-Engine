#pragma once

#include <juce_core/juce_core.h>

namespace ce::assets
{
// Immutable asset packs live at one exact id/version address in the suite VFS.
// Games retain that address in their own data; local source folders are only
// installation input and are never runtime authority.
class AssetPackStore final
{
public:
    struct Manifest
    {
        juce::String id, version, title, defaultScene;
        struct Asset { juce::String id, kind, payload, generator; };
        juce::Array<Asset> assets;
    };

    static bool installDirectory(const juce::File& sourceRoot, Manifest& manifest, juce::String& errorMessage);
    static bool readManifest(const juce::String& packId, const juce::String& version,
                             Manifest& manifest, juce::String& errorMessage);
    static bool readEntry(const juce::String& packId, const juce::String& version,
                          const juce::String& relativePath, juce::MemoryBlock& data,
                          juce::String& errorMessage);
    static bool materializePack(const juce::String& packId, const juce::String& version,
                                juce::File& directory, juce::String& errorMessage);
    [[nodiscard]] static juce::String vfsRoot(const juce::String& packId, const juce::String& version);
};
} // namespace ce::assets
