#include "Scene/AssetCatalog.h"

#include <iostream>

#include "Render/Import/GltfLoader.h"
#include "Render/Scene/ProceduralMesh.h"

namespace ce::scene {

void AssetCatalog::AddProcedural(const juce::String& name, const std::vector<Vertex>& vertices,
                                  const std::vector<GLuint>& indices, juce::Vector3D<float> albedo) {
    auto mesh = std::make_shared<Mesh>();
    mesh->Upload(vertices, indices);

    auto material = std::make_shared<Material>();
    material->albedo = albedo;
    material->metallic = 0.1f;
    material->roughness = 0.6f;

    assets_.emplace(name.toStdString(), Asset{ mesh, material });
    names_.push_back(name);
}

void AssetCatalog::LoadBuiltins(assets::VirtualFileSystem& vfs) {
    std::vector<Vertex> cubeVertices;
    std::vector<GLuint> cubeIndices;
    GenerateCube(cubeVertices, cubeIndices);
    AddProcedural("Cube", cubeVertices, cubeIndices, { 0.6f, 0.3f, 0.7f });

    std::vector<Vertex> sphereVertices;
    std::vector<GLuint> sphereIndices;
    GenerateUVSphere(24, 48, sphereVertices, sphereIndices);
    AddProcedural("Sphere", sphereVertices, sphereIndices, { 0.2f, 0.5f, 0.8f });

    LoadedModel model;
    if (!LoadGltfFromVfs(vfs, "BoxTextured.gltf", model) || model.primitives.empty()) {
        std::cout << "[catalog] BoxTextured failed to load from VFS; catalog will only have procedural shapes."
                   << std::endl;
        return;
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->Upload(model.primitives.front().vertices, model.primitives.front().indices);

    auto material = std::make_shared<Material>();
    const int materialIndex = model.primitives.front().materialIndex;
    if (materialIndex >= 0 && materialIndex < static_cast<int>(model.materials.size())) {
        const auto& srcMaterial = model.materials[static_cast<std::size_t>(materialIndex)];
        material->albedo = srcMaterial.baseColorFactor;
        material->metallic = srcMaterial.metallicFactor;
        material->roughness = srcMaterial.roughnessFactor;

        if (srcMaterial.baseColorTextureVirtualPath.isNotEmpty()) {
            juce::MemoryBlock textureBytes;
            if (vfs.ReadFile(srcMaterial.baseColorTextureVirtualPath, textureBytes)) {
                auto texture = std::make_unique<gl::Texture2D>();
                if (texture->LoadFromMemory(textureBytes.getData(), textureBytes.getSize(),
                                             srcMaterial.baseColorTextureVirtualPath)) {
                    material->albedoTexture = texture.get();
                }
                ownedTextures_.push_back(std::move(texture));
            }
        }
    }

    assets_.emplace("BoxTextured", Asset{ mesh, material });
    names_.push_back("BoxTextured");

    std::cout << "[catalog] loaded builtins: " << names_.size() << " assets" << std::endl;
}

const AssetCatalog::Asset* AssetCatalog::Find(const juce::String& name) const {
    auto it = assets_.find(name.toStdString());
    return it == assets_.end() ? nullptr : &it->second;
}

} // namespace ce::scene
