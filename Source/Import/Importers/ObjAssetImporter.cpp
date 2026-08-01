#include "Import/Importers/ObjAssetImporter.h"

#include <mutex>

#include "Render/Import/ObjLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"
#include "Scene/AssetPlacement.h"
#include "Scene/Components.h"

namespace ce::import {

ImportResult ObjAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr || context.world == nullptr) {
        return ImportResult::Failed("OBJ import needs a live world, asset catalog, and viewport.");
    }

    LoadedModel model;
    if (!LoadObj(sourceFile, model) || model.primitives.empty()) {
        return ImportResult::Failed("Failed to parse " + sourceFile.getFileName() + " (see log for details).");
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] { added = context.catalog->AddFromModel(assetName, model); }, /*blockUntilFinished=*/true);

    if (!added) {
        return ImportResult::Failed("Failed to build GPU resources for " + sourceFile.getFileName() + ".");
    }

    // See GltfAssetImporter::Import's matching comment -- same "drop it
    // straight into the scene" behavior for both formats.
    const auto asset = context.catalog->Find(assetName);
    {
        std::lock_guard<std::mutex> lock(context.world->RegistryMutex());
        scene::PlaceAssetEntity(*context.world, asset, assetName, scene::ToVec3(context.viewport->SpawnPosition()));
    }

    return ImportResult::Ok(assetName + " imported and placed in the scene.");
}

} // namespace ce::import
