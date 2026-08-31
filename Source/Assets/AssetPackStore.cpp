#include "Assets/AssetPackStore.h"

#include <creation/services/SuiteVfsServiceClient.h>

namespace ce::assets
{
namespace
{
bool isSafeRelativePath(const juce::String& path)
{
    return path.isNotEmpty() && ! path.startsWithChar('/') && ! path.contains("..") && ! path.containsChar('\\');
}

bool parseManifest(const juce::MemoryBlock& data, AssetPackStore::Manifest& manifest, juce::String& error)
{
    const auto parsed = juce::JSON::parse(juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize())));
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr || static_cast<int>(object->getProperty("formatVersion")) != 1)
    {
        error = "Asset Pack manifest is not valid version 1 JSON.";
        return false;
    }
    manifest.id = object->getProperty("id").toString();
    manifest.version = object->getProperty("version").toString();
    manifest.title = object->getProperty("title").toString();
    manifest.defaultScene = object->getProperty("defaultScene").toString();
    if (manifest.id.isEmpty() || manifest.version.isEmpty() || manifest.title.isEmpty())
    {
        error = "Asset Pack manifest requires id, version, and title.";
        return false;
    }
    if (manifest.defaultScene.isNotEmpty() && ! isSafeRelativePath(manifest.defaultScene))
    {
        error = "Asset Pack defaultScene must be inside the pack.";
        return false;
    }
    manifest.assets.clear();
    const auto* assets = object->getProperty("assets").getArray();
    if (assets == nullptr || assets->isEmpty())
    {
        error = "Asset Pack manifest must declare assets.";
        return false;
    }
    for (const auto& entry : *assets)
    {
        const auto* asset = entry.getDynamicObject();
        const auto payload = asset == nullptr ? juce::String{} : asset->getProperty("payload").toString();
        if (asset == nullptr || asset->getProperty("id").toString().isEmpty() ||
            asset->getProperty("kind").toString().isEmpty() || (payload.isNotEmpty() && ! isSafeRelativePath(payload)))
        {
            error = "Asset Pack manifest contains an invalid asset declaration.";
            return false;
        }
        manifest.assets.add({ asset->getProperty("id").toString(), asset->getProperty("kind").toString(),
                              payload, asset->getProperty("generator").toString() });
    }
    return true;
}

bool readFile(const juce::File& file, juce::MemoryBlock& data, juce::String& error)
{
    if (file.loadFileAsData(data)) return true;
    error = "Could not read Asset Pack file: " + file.getFullPathName();
    return false;
}
} // namespace

juce::String AssetPackStore::vfsRoot(const juce::String& id, const juce::String& version)
{
    return "asset-packs/" + id + "/" + version;
}

bool AssetPackStore::readManifest(const juce::String& id, const juce::String& version,
                                  Manifest& manifest, juce::String& error)
{
    creation::services::SuiteVfsServiceClient client;
    juce::MemoryBlock data;
    if (! client.discover() || ! client.readEntry(vfsRoot(id, version) + "/pack.json", data))
    {
        error = "Asset Pack is not installed in the suite VFS: " + id + " " + version;
        return false;
    }
    return parseManifest(data, manifest, error) && manifest.id == id && manifest.version == version;
}

bool AssetPackStore::readEntry(const juce::String& id, const juce::String& version,
                               const juce::String& path, juce::MemoryBlock& data, juce::String& error)
{
    if (! isSafeRelativePath(path))
    {
        error = "Asset Pack entry must be inside the pack.";
        return false;
    }
    creation::services::SuiteVfsServiceClient client;
    if (client.discover() && client.readEntry(vfsRoot(id, version) + "/" + path, data)) return true;
    error = "Could not read Asset Pack entry: " + path;
    return false;
}

bool AssetPackStore::installDirectory(const juce::File& sourceRoot, Manifest& manifest, juce::String& error)
{
    juce::MemoryBlock sourceManifest;
    if (! sourceRoot.isDirectory() || ! readFile(sourceRoot.getChildFile("pack.json"), sourceManifest, error) ||
        ! parseManifest(sourceManifest, manifest, error)) return false;

    creation::services::SuiteVfsServiceClient client;
    if (! client.discover())
    {
        error = "Could not reach the suite VFS service to install the Asset Pack.";
        return false;
    }
    const auto root = vfsRoot(manifest.id, manifest.version);
    juce::MemoryBlock existing;
    if (client.readEntry(root + "/pack.json", existing))
    {
        if (existing == sourceManifest) return true;
        error = "A different Asset Pack already occupies " + manifest.id + " " + manifest.version + ".";
        return false;
    }

    juce::Array<juce::File> files;
    sourceRoot.findChildFiles(files, juce::File::findFiles, true);
    for (const auto& file : files)
    {
        const auto relative = file.getRelativePathFrom(sourceRoot).replaceCharacter('\\', '/');
        juce::MemoryBlock data;
        if (! isSafeRelativePath(relative) || ! readFile(file, data, error) || ! client.writeEntry(root + "/" + relative, data))
        {
            if (error.isEmpty()) error = "Could not install Asset Pack entry: " + relative;
            return false;
        }
    }
    return true;
}

bool AssetPackStore::materializePack(const juce::String& id, const juce::String& version,
                                     juce::File& directory, juce::String& error)
{
    Manifest manifest;
    if (! readManifest(id, version, manifest, error)) return false;
    creation::services::SuiteVfsServiceClient client;
    juce::StringArray paths;
    if (! client.discover() || ! client.listEntries(paths))
    {
        error = "Could not enumerate Asset Pack entries in the suite VFS.";
        return false;
    }

    const auto root = vfsRoot(id, version) + "/";
    const auto cacheName = id.replaceCharacters("/:\\", "___") + "-" + version.replaceCharacters("/:\\", "___");
    directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("CreationEngineAssetPackCache").getChildFile(cacheName);
    if (! directory.createDirectory())
    {
        error = "Could not create the temporary Asset Pack decoder cache.";
        return false;
    }
    for (const auto& fullPath : paths)
    {
        if (! fullPath.startsWith(root)) continue;
        const auto relative = fullPath.substring(root.length());
        juce::MemoryBlock data;
        const auto destination = directory.getChildFile(relative);
        if (! isSafeRelativePath(relative) || ! client.readEntry(fullPath, data) ||
            ! destination.getParentDirectory().createDirectory() ||
            ! destination.replaceWithData(data.getData(), data.getSize()))
        {
            error = "Could not materialize Asset Pack entry: " + relative;
            return false;
        }
    }
    return true;
}
} // namespace ce::assets
