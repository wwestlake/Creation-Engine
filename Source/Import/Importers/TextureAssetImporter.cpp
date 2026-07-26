#include "Import/Importers/TextureAssetImporter.h"

#include "Render/GL/Texture2D.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/Scene/ProceduralMesh.h"
#include "Render/Scene/Vertex.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"

namespace ce::import {

ImportResult TextureAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.catalog == nullptr || context.viewport == nullptr) {
        return ImportResult::Failed("Texture import needs a live asset catalog and viewport.");
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();

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
    return ImportResult::Ok(assetName + " imported as a textured cube -- available in the \"+ Add\" menu.");
}

} // namespace ce::import
