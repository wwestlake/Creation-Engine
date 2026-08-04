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

    const std::lock_guard<std::mutex> lock(mutex_);
    if (assets_.find(name.toStdString()) == assets_.end()) {
        names_.push_back(name);
    }
    assets_[name.toStdString()] = Asset{ mesh, material };
}

void AssetCatalog::LoadBuiltins(creation::assets::VirtualFileSystem& vfs) {
    // Guards against a GL context recreation (e.g. the window moving to
    // a monitor with a different pixel format) re-running this: without
    // it, every procedural Mesh this builds a second time would be a
    // real GPU-resource leak (uploaded, then immediately orphaned, since
    // re-registering under the same name just replaces the catalog
    // entry) -- same class of bug ViewportComponent::hasSeededDemoScene_
    // already guards against for the demo entity.
    if (hasLoadedBuiltins_) {
        return;
    }
    hasLoadedBuiltins_ = true;

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

    AddFromModel("BoxTextured", model, &vfs);

    std::cout << "[catalog] loaded builtins: " << names_.size() << " assets" << std::endl;
}

bool AssetCatalog::AddFromModel(const juce::String& name, const LoadedModel& model, creation::assets::VirtualFileSystem* vfs) {
    if (model.primitives.empty()) {
        return false;
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->Upload(model.primitives.front().vertices, model.primitives.front().indices);

    auto material = std::make_shared<Material>();
    std::unique_ptr<gl::Texture2D> texture;
    const int materialIndex = model.primitives.front().materialIndex;
    if (materialIndex >= 0 && materialIndex < static_cast<int>(model.materials.size())) {
        const auto& srcMaterial = model.materials[static_cast<std::size_t>(materialIndex)];
        material->albedo = srcMaterial.baseColorFactor;
        material->metallic = srcMaterial.metallicFactor;
        material->roughness = srcMaterial.roughnessFactor;

        if (vfs != nullptr && srcMaterial.baseColorTextureVirtualPath.isNotEmpty()) {
            juce::MemoryBlock textureBytes;
            if (vfs->readFile(srcMaterial.baseColorTextureVirtualPath, textureBytes)) {
                texture = std::make_unique<gl::Texture2D>();
                if (texture->LoadFromMemory(textureBytes.getData(), textureBytes.getSize(),
                                             srcMaterial.baseColorTextureVirtualPath)) {
                    material->albedoTexture = texture.get();
                }
            }
        } else if (srcMaterial.baseColorTexturePath.existsAsFile()) {
            texture = std::make_unique<gl::Texture2D>();
            if (texture->LoadFromFile(srcMaterial.baseColorTexturePath)) {
                material->albedoTexture = texture.get();
            }
        }
    }

    std::shared_ptr<Skeleton> skeleton;
    if (model.skin.has_value()) {
        material->isSkinned = true;
        skeleton = std::make_shared<Skeleton>();
        skeleton->joints.reserve(model.skin->joints.size());
        for (const auto& loadedJoint : model.skin->joints) {
            Skeleton::Joint joint;
            joint.name = loadedJoint.name;
            joint.parentIndex = loadedJoint.parentIndex;
            joint.inverseBindMatrix = loadedJoint.inverseBindMatrix;
            joint.localBindTransform = loadedJoint.localBindTransform;
            joint.bindTranslation = loadedJoint.bindTranslation;
            joint.bindRotation[0] = loadedJoint.bindRotation[0];
            joint.bindRotation[1] = loadedJoint.bindRotation[1];
            joint.bindRotation[2] = loadedJoint.bindRotation[2];
            joint.bindRotation[3] = loadedJoint.bindRotation[3];
            joint.bindScale = loadedJoint.bindScale;
            skeleton->joints.push_back(std::move(joint));
        }
    }

    std::shared_ptr<std::vector<AnimationClip>> animationClips;
    if (!model.animations.empty()) {
        animationClips = std::make_shared<std::vector<AnimationClip>>(model.animations);
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (texture != nullptr) {
        ownedTextures_.push_back(std::move(texture));
    }
    if (assets_.find(name.toStdString()) == assets_.end()) {
        names_.push_back(name);
    }
    assets_[name.toStdString()] = Asset{ mesh, material, skeleton, animationClips };
    return true;
}

bool AssetCatalog::Add(const juce::String& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material,
                        std::unique_ptr<gl::Texture2D> ownedTexture) {
    if (mesh == nullptr || material == nullptr) {
        return false;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (ownedTexture != nullptr) {
        ownedTextures_.push_back(std::move(ownedTexture));
    }
    if (assets_.find(name.toStdString()) == assets_.end()) {
        names_.push_back(name);
    }
    assets_[name.toStdString()] = Asset{ std::move(mesh), std::move(material) };
    return true;
}

AssetCatalog::Asset AssetCatalog::Find(const juce::String& name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(name.toStdString());
    return it == assets_.end() ? Asset{} : it->second;
}

std::vector<juce::String> AssetCatalog::Names() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return names_;
}

} // namespace ce::scene
