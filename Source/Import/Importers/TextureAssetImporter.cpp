#include "Import/Importers/TextureAssetImporter.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/ProjectContentAssetStore.h"
#include "Render/GL/Texture2D.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/Scene/ProceduralMesh.h"
#include "Render/Scene/Vertex.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"

namespace ce::import {

ImportResult TextureAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("Texture import needs an open game, asset catalog, and viewport.");
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();

    creation::assets::AssetDescriptor sourceDescriptor;
    juce::String persistenceError;
    if (! assets::ProjectContentAssetStore::importSource(*context.projectSession, context.gameAssetRoot, sourceFile,
                                                          creation::assets::AssetKind::render, "Texture",
                                                          { "texture" }, sourceDescriptor, persistenceError))
        return ImportResult::Failed("Could not store texture source in project content: " + persistenceError);

    bool added = false;
    context.viewport->RunOnGLThread(
        [&] {
            auto texture = std::make_unique<gl::Texture2D>();
            if (!texture->LoadFromFile(sourceFile)) {
                return;
            }

            auto material = std::make_shared<Material>();
            material->albedoTexture = texture.get();
            material->albedo = { 1.0f, 1.0f, 1.0f }; // white -- let the texture's own colour show through unmodified.
            material->roughness = 0.8f;
            material->metallic = 0.0f;

            std::vector<Vertex> vertices;
            std::vector<GLuint> indices;
            GenerateCube(vertices, indices);
            auto mesh = std::make_shared<Mesh>();
            mesh->Upload(vertices, indices);

            added = context.catalog->Add(assetName, mesh, material, std::move(texture));
        },
        /*blockUntilFinished=*/true);

    if (!added) {
        return ImportResult::Failed("Failed to decode " + sourceFile.getFileName() + " (see log for details).");
    }
    if (! context.catalog->SetSourceIdentity(assetName, sourceDescriptor.id, sourceDescriptor.versionId) ||
        ! context.catalog->AddAlias(sourceDescriptor.id, assetName))
        return ImportResult::Failed("The imported texture could not be registered with its project asset identity.");
    return ImportResult::Ok(assetName + " stored in project content as a textured cube -- available in the \"+ Add\" menu.");
}

ImportResult TextureAssetImporter::Reimport(const juce::File& sourceFile,
                                            const creation::assets::AssetDescriptor& existingAsset,
                                            ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("Texture reimport needs an open game, asset catalog, and viewport.");
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
        [&] {
            auto texture = std::make_unique<gl::Texture2D>();
            if (!texture->LoadFromFile(sourceFile)) {
                return;
            }

            auto material = std::make_shared<Material>();
            material->albedoTexture = texture.get();
            material->albedo = { 1.0f, 1.0f, 1.0f };
            material->roughness = 0.8f;
            material->metallic = 0.0f;

            std::vector<Vertex> vertices;
            std::vector<GLuint> indices;
            GenerateCube(vertices, indices);
            auto mesh = std::make_shared<Mesh>();
            mesh->Upload(vertices, indices);

            added = context.catalog->Add(existingAsset.displayName, mesh, material, std::move(texture));
        },
        /*blockUntilFinished=*/true);

    if (!added) {
        return ImportResult::Failed("Failed to decode " + sourceFile.getFileName() + " (see log for details).");
    }
    if (! context.catalog->SetSourceIdentity(existingAsset.displayName, newDescriptor.id, newDescriptor.versionId) ||
        ! context.catalog->AddAlias(newDescriptor.id, existingAsset.displayName))
        return ImportResult::Failed("The reimported texture could not be re-registered with its project asset identity.");
    return ImportResult::Ok(existingAsset.displayName + " updated to a new version.");
}

} // namespace ce::import
