#include "Import/Importers/ObjAssetImporter.h"

#include <mutex>

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

    creation::assets::AssetDescriptor sourceDescriptor;
    juce::String persistenceError;
    if (! assets::ProjectContentAssetStore::importSource(*context.projectSession, context.gameAssetRoot, sourceFile,
                                                          creation::assets::AssetKind::render, "Model",
                                                          { "model", "obj" }, sourceDescriptor, persistenceError))
        return ImportResult::Failed("Could not store OBJ source in project content: " + persistenceError);

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();

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

} // namespace ce::import
