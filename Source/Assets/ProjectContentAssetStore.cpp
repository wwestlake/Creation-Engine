#include "Assets/ProjectContentAssetStore.h"

#include <creation/assets/ProjectAssetService.h>

#include "Render/Import/GltfLoader.h"
#include "Render/Import/ObjLoader.h"

namespace ce::assets
{
bool ProjectContentAssetStore::importSource(creation::assets::ProjectSession& session,
                                            const juce::String& gameAssetRoot,
                                            const juce::File& sourceFile,
                                            creation::assets::AssetKind kind,
                                            const juce::String& category,
                                            const juce::StringArray& tags,
                                            creation::assets::AssetDescriptor& descriptor,
                                            juce::String& errorMessage,
                                            const juce::String& displayNameOverride,
                                            const juce::String& description)
{
    if (! session.isValid())
    {
        errorMessage = "Open a Creation Engine game before importing assets.";
        return false;
    }

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = kind;
    options.displayName = displayNameOverride.isNotEmpty() ? displayNameOverride : sourceFile.getFileNameWithoutExtension();
    options.description = description;
    const auto sourceBundleRoot = gameAssetRoot + "sources/" + sourceFile.getFileNameWithoutExtension() + "/";
    options.logicalPath = sourceBundleRoot + sourceFile.getFileName();
    options.category = category;
    options.mediaType = sourceFile.getFileExtension().substring(1).toLowerCase();
    options.sourceApp = "Creation Engine";
    options.sourceTool = "Engine Import";
    options.importerId = "creation-engine-source-import";
    options.importerVersion = "1";
    options.importSettings = "sourceBundle=" + sourceBundleRoot;
    options.tags = tags;

    if (! creation::assets::ProjectAssetService::importFile(session, sourceFile, options, descriptor, errorMessage))
        return false;

    // A model is a source bundle, not merely its entry file. Preserve its
    // sibling data and texture files in the same VFS folder so a later reopen
    // can materialize the exact source relationship its decoder requires.
    if (category == "Model")
    {
        juce::Array<juce::File> siblings;
        sourceFile.getParentDirectory().findChildFiles(siblings, juce::File::findFiles, false);
        for (const auto& sibling : siblings)
        {
            if (sibling == sourceFile) continue;
            if (! session.writeEntryFromFile(sourceBundleRoot + sibling.getFileName(), sibling, errorMessage))
                return false;
        }
    }
    return session.commit(errorMessage);
}

bool ProjectContentAssetStore::loadRenderable(const creation::assets::ProjectSession& session,
                                              const creation::suite::SuiteSettings& settings,
                                              const juce::String& assetId,
                                              const juce::String& versionId,
                                              ce::LoadedModel& model,
                                              creation::assets::MaterializedAssetLease& lease,
                                              juce::String& errorMessage)
{
    creation::assets::AssetRef reference;
    reference.id = assetId;
    reference.versionId = versionId;
    reference.mode = creation::assets::AssetReferenceMode::exact;

    const auto* descriptor = creation::assets::ProjectAssetService::resolveAsset(session, reference);
    if (descriptor == nullptr)
    {
        errorMessage = "The requested model is not present in the active game's managed content.";
        return false;
    }

    if (! creation::assets::ProjectAssetService::materializeAsset(
            session, settings, reference, creation::assets::MaterializationAccess::readOnly, lease, errorMessage))
        return false;

    const auto releaseAndFail = [&lease, &errorMessage]() {
        juce::String releaseError;
        creation::assets::AssetMaterializer::releaseLease(lease, releaseError);
        return false;
    };

    const auto bundleRoot = descriptor->logicalPath.upToLastOccurrenceOf("/", false, true) + "/";
    const auto entryPaths = session.listEntryPaths();
    for (const auto& entryPath : entryPaths)
    {
        if (! entryPath.startsWith(bundleRoot) || entryPath == descriptor->logicalPath) continue;
        juce::MemoryBlock dependencyData;
        if (! session.readEntry(entryPath, dependencyData))
        {
            errorMessage = "A model dependency could not be read from project content: " + entryPath;
            return releaseAndFail();
        }
        const auto relativePath = entryPath.substring(bundleRoot.length());
        const auto destination = lease.materializedFile.getParentDirectory().getChildFile(relativePath);
        destination.getParentDirectory().createDirectory();
        if (! destination.replaceWithData(dependencyData.getData(), dependencyData.getSize()))
        {
            errorMessage = "A model dependency could not be materialized: " + relativePath;
            return releaseAndFail();
        }
    }

    const auto extension = lease.materializedFile.getFileExtension().toLowerCase();
    bool loaded = false;
    if (extension == ".gltf" || extension == ".glb")
        loaded = LoadGltf(lease.materializedFile, model);
    else if (extension == ".obj")
        loaded = LoadObj(lease.materializedFile, model);
    else
        errorMessage = "The asset is not a supported renderable model: " + extension;

    if (loaded) return true;
    if (errorMessage.isEmpty()) errorMessage = "The managed model could not be decoded.";
    return releaseAndFail();
}
} // namespace ce::assets
