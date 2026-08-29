#include "Import/Importers/FbxAssetImporter.h"

#include "Render/Import/FbxLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"
#include "Scene/AssetPlacement.h"
#include "Scene/Components.h"
#include <creation/assets/ProjectAssetService.h>

#include <mutex>

namespace ce::import {

ImportResult FbxAssetImporter::Import(const juce::File& sourceFile, ImportContext& context)
{
    if (context.catalog == nullptr || context.viewport == nullptr || context.world == nullptr)
        return ImportResult::Failed("FBX import needs a live world, asset catalog, and viewport.");

    LoadedModel model;
    juce::String parseError;
    if (! LoadFbx(sourceFile, model, parseError))
        return ImportResult::Failed(parseError);

    if (context.projectSession != nullptr && context.projectSession->isValid()) {
        creation::assets::ProjectAssetService::ImportOptions options;
        options.kind = creation::assets::AssetKind::render;
        options.displayName = sourceFile.getFileNameWithoutExtension();
        options.logicalPath = "Assets/Source/Models/" + sourceFile.getFileName();
        options.category = "FBX Source";
        options.mediaType = "model/fbx";
        options.sourceApp = "Creation Engine";
        options.sourceTool = "ufbx";
        options.importerId = "creation-engine.fbx";
        options.importerVersion = "1";
        options.importSettings = "{\"sourceUnit\":\"decimeter\",\"targetUnit\":\"meter\"}";

        creation::assets::AssetDescriptor sourceDescriptor;
        juce::String errorMessage;
        if (! creation::assets::ProjectAssetService::importFile(
                *context.projectSession, sourceFile, options, sourceDescriptor, errorMessage))
            return ImportResult::Failed("FBX parsed, but source preservation failed: " + errorMessage);
        if (! context.projectSession->commit(errorMessage))
            return ImportResult::Failed("FBX source was copied, but the project manifest could not be committed: " + errorMessage);
    }

    const auto assetName = sourceFile.getFileNameWithoutExtension();
    bool added = false;
    context.viewport->RunOnGLThread([&] { added = context.catalog->AddFromModel(assetName, model); }, true);
    if (! added)
        return ImportResult::Failed("Failed to build GPU resources for " + sourceFile.getFileName() + ".");

    const auto asset = context.catalog->Find(assetName);
    {
        std::lock_guard<std::mutex> lock(context.world->RegistryMutex());
        scene::PlaceAssetEntity(*context.world, asset, assetName, scene::ToVec3(context.viewport->SpawnPosition()));
    }

    return ImportResult::Ok(assetName + " imported, preserved as a VFS source asset, and placed in the scene.");
}

} // namespace ce::import
