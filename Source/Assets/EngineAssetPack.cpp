#include "Assets/EngineAssetPack.h"

namespace ce::assets
{
juce::String EngineAssetPack::vfsRoot()
{
    return AssetPackStore::vfsRoot(packId, version);
}

juce::String EngineAssetPack::manifestPath() { return vfsRoot() + "/pack.json"; }
juce::String EngineAssetPack::defaultScenePath() { return vfsRoot() + "/scenes/DefaultScene.xml"; }

bool EngineAssetPack::ensureInstalled(juce::String& errorMessage)
{
    const auto sourceRoot = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile).getParentDirectory().getChildFile("EnginePack");
    if (! sourceRoot.isDirectory())
    {
        errorMessage = "The shipped Creation Engine Pack is missing from the editor installation.";
        return false;
    }

    AssetPackStore::Manifest installed;
    if (! AssetPackStore::installDirectory(sourceRoot, installed, errorMessage)) return false;
    if (installed.id != packId || installed.version != version || installed.defaultScene.isEmpty())
    {
        errorMessage = "The shipped Creation Engine Pack has an unexpected identity or does not declare a Default Scene.";
        return false;
    }
    return true;
}

bool EngineAssetPack::readDefaultScene(juce::MemoryBlock& sceneData, juce::String& errorMessage)
{
    if (! ensureInstalled(errorMessage)) return false;
    return AssetPackStore::readEntry(packId, version, "scenes/DefaultScene.xml", sceneData, errorMessage);
}
} // namespace ce::assets
