#include "Import/Importers/ObjAssetImporter.h"

#include <mutex>

#include <creation/assets/ProjectAssetService.h>

#include "Assets/ProjectContentAssetStore.h"
#include "Render/Import/ObjLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"
#include "Scene/AssetPlacement.h"
#include "Scene/Components.h"

namespace ce::import {

ImportResult ObjAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr || context.world == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("OBJ import needs an open game, world, asset catalog, and viewport.");
    }

    LoadedModel model;
    if (!LoadObj(sourceFile, model) || model.primitives.empty()) {
        return ImportResult::Failed("Failed to parse " + sourceFile.getFileName() + " (see log for details).");
    }

    juce::StringArray tags{ "model", "obj" };
    tags.addArray(context.pendingTags);

    creation::assets::AssetDescriptor sourceDescriptor;
    juce::String persistenceError;
    if (! assets::ProjectContentAssetStore::importSource(*context.projectSession, context.gameAssetRoot, sourceFile,
                                                          creation::assets::AssetKind::render, "Model", tags,
                                                          sourceDescriptor, persistenceError,
                                                          context.pendingDisplayName, context.pendingDescription))
        return ImportResult::Failed("Could not store OBJ source in project content: " + persistenceError);

    const juce::String assetName =
        context.pendingDisplayName.isNotEmpty() ? context.pendingDisplayName : sourceFile.getFileNameWithoutExtension();

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] { added = context.catalog->AddFromModel(assetName, model); }, /*blockUntilFinished=*/true);

    if (!added) {
        return ImportResult::Failed("Failed to build GPU resources for " + sourceFile.getFileName() + ".");
    }
    if (! context.catalog->SetSourceIdentity(assetName, sourceDescriptor.id, sourceDescriptor.versionId) ||
        ! context.catalog->AddAlias(sourceDescriptor.id, assetName))
        return ImportResult::Failed("The imported model could not be registered with its project asset identity.");

    // See GltfAssetImporter::Import's matching comment -- same "drop it
    // straight into the scene" behavior for both formats.
    const auto asset = context.catalog->Find(assetName);
    {
        std::lock_guard<std::mutex> lock(context.world->RegistryMutex());
        scene::PlaceAssetEntity(*context.world, asset, assetName, scene::ToVec3(context.viewport->SpawnPosition()),
                                entt::null, sourceDescriptor.id, sourceDescriptor.versionId);
    }

    return ImportResult::Ok(assetName + " stored in project content and placed in the scene.");
}

ImportResult ObjAssetImporter::Reimport(const juce::File& sourceFile,
                                        const creation::assets::AssetDescriptor& existingAsset,
                                        ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("OBJ reimport needs an open game, asset catalog, and viewport.");
    }

    LoadedModel model;
    if (!LoadObj(sourceFile, model) || model.primitives.empty()) {
        return ImportResult::Failed("Failed to parse " + sourceFile.getFileName() + " (see log for details).");
    }

    creation::assets::ProjectAssetService::ImportOptions overrides;
    creation::assets::AssetDescriptor newDescriptor;
    juce::String persistenceError;
    if (! creation::assets::ProjectAssetService::createNewVersion(*context.projectSession, existingAsset, sourceFile,
                                                                   overrides, newDescriptor, persistenceError))
        return ImportResult::Failed("Could not save the new version: " + persistenceError);
    if (! context.projectSession->commit(persistenceError))
        return ImportResult::Failed("New version saved, but the project manifest could not be committed: " + persistenceError);

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] { added = context.catalog->AddFromModel(existingAsset.displayName, model); }, /*blockUntilFinished=*/true);
    if (!added) {
        return ImportResult::Failed("Failed to rebuild GPU resources for " + sourceFile.getFileName() + ".");
    }
    if (! context.catalog->SetSourceIdentity(existingAsset.displayName, newDescriptor.id, newDescriptor.versionId) ||
        ! context.catalog->AddAlias(newDescriptor.id, existingAsset.displayName))
        return ImportResult::Failed("The reimported model could not be re-registered with its project asset identity.");

    return ImportResult::Ok(existingAsset.displayName + " updated to a new version.");
}

} // namespace ce::import
