#include "Assets/EngineAssetPack.h"

namespace ce::assets
{
juce::String EngineAssetPack::vfsRoot()
{
    return AssetPackStore::vfsRoot(packId, version);
}

juce::String EngineAssetPack::manifestPath() { return vfsRoot() + "/pack.json"; }
juce::String EngineAssetPack::defaultScenePath() { return vfsRoot() + "/scenes/DefaultScene.xml"; }
juce::String EngineAssetPack::defaultInputMappingPath() { return "input-mappings/standard-desktop-controls.json"; }

bool EngineAssetPack::ensureInstalled(juce::String& errorMessage)
{
    const auto sourceRoot = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile).getParentDirectory().getChildFile("EnginePack");
    if (! sourceRoot.isDirectory())
    {
        errorMessage = "The shipped Djehuti Engine Pack is missing from the editor installation.";
        return false;
    }

    AssetPackStore::Manifest installed;
    if (! AssetPackStore::installDirectory(sourceRoot, installed, errorMessage)) return false;
    if (installed.id != packId || installed.version != version || installed.defaultScene.isEmpty() || installed.defaultInputMapping.isEmpty())
    {
        errorMessage = "The shipped Djehuti Engine Pack has an unexpected identity or does not declare its defaults.";
        return false;
    }
    return true;
}

bool EngineAssetPack::readDefaultScene(juce::MemoryBlock& sceneData, juce::String& errorMessage)
{
    return readScene("DefaultScene", sceneData, errorMessage);
}

bool EngineAssetPack::readDefaultInputMapping(juce::MemoryBlock& mappingData, juce::String& errorMessage)
{
    return readEntry(defaultInputMappingPath(), mappingData, errorMessage);
}

bool EngineAssetPack::readEntry(const juce::String& relativePath, juce::MemoryBlock& data, juce::String& errorMessage)
{
    if (! ensureInstalled(errorMessage)) return false;
    return AssetPackStore::readEntry(packId, version, relativePath, data, errorMessage);
}

juce::Array<EngineAssetPack::CharacterAsset> EngineAssetPack::characterAssets()
{
    juce::String error;
    AssetPackStore::Manifest manifest;
    if (!ensureInstalled(error) || !AssetPackStore::readManifest(packId, version, manifest, error))
        return {};

    juce::Array<CharacterAsset> assets;
    for (const auto& asset : manifest.assets)
        if (asset.kind == "character")
            assets.add({ asset.id, asset.title.isNotEmpty() ? asset.title : asset.id });
    return assets;
}

juce::StringArray EngineAssetPack::characterAssetIds()
{
    juce::StringArray ids;
    for (const auto& asset : characterAssets()) ids.add(asset.id);
    return ids;
}

bool EngineAssetPack::readScene(const juce::String& templateSceneId, juce::MemoryBlock& sceneData, juce::String& errorMessage)
{
    if (! ensureInstalled(errorMessage)) return false;
    return readEntry("scenes/" + templateSceneId + ".xml", sceneData, errorMessage);
}
} // namespace ce::assets
