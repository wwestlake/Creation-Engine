#include "Scene/AssetCatalog.h"

#include <iostream>

#include "Assets/AssetPackStore.h"
#include "Render/Import/GltfLoader.h"
#include "Render/Scene/ProceduralMesh.h"

namespace ce::scene {

juce::String AssetCatalog::PackAssetKey(const juce::String& packId, const juce::String& version,
                                        const juce::String& assetId)
{
    return packId + "@" + version + ":" + assetId;
}

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
    assets_[name.toStdString()] = Asset{ name, {}, {}, {}, mesh, material };
}

bool AssetCatalog::LoadAssetPack(const juce::String& packId, const juce::String& version, juce::String& errorMessage) {
    const auto key = (packId + "@" + version).toStdString();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (loadedPacks_.contains(key)) return true;
    }

    assets::AssetPackStore::Manifest manifest;
    if (! assets::AssetPackStore::readManifest(packId, version, manifest, errorMessage)) return false;
    juce::File cacheDirectory;
    if (! assets::AssetPackStore::materializePack(packId, version, cacheDirectory, errorMessage)) return false;

    for (const auto& declared : manifest.assets)
    {
        const auto cacheKey = PackAssetKey(packId, version, declared.id);
        if (declared.kind == "procedural-mesh")
        {
            std::vector<Vertex> vertices;
            std::vector<GLuint> indices;
            if (declared.generator == "cube") GenerateCube(vertices, indices);
            else if (declared.generator == "uv-sphere") GenerateUVSphere(24, 48, vertices, indices);
            else
            {
                errorMessage = "Unsupported procedural mesh generator: " + declared.generator;
                return false;
            }
            AddProcedural(cacheKey, vertices, indices,
                          declared.generator == "cube" ? juce::Vector3D<float>{ 0.6f, 0.3f, 0.7f }
                                                        : juce::Vector3D<float>{ 0.2f, 0.5f, 0.8f });
            SetSourceIdentity(cacheKey, declared.id, {}, packId, version);
            // The exact pack-qualified key is the authoritative identity.  The
            // first pack loaded also establishes the conventional engine asset
            // name (for example, "Cube") used by scene content authored before
            // pack identity was recorded on the object.
            if (Find(declared.id).mesh == nullptr)
                AddAlias(declared.id, cacheKey);
        }
        else if (declared.kind == "model")
        {
            LoadedModel model;
            if (declared.payload.isEmpty() || ! LoadGltf(cacheDirectory.getChildFile(declared.payload), model) ||
                model.primitives.empty() || ! AddFromModel(cacheKey, model))
            {
                // One broken optional model must not take down the whole pack --
                // scenes that never reference this asset (e.g. the default
                // starter scene, which only uses the procedural Cube/Sphere)
                // would otherwise fail to render anything at all over an asset
                // they never asked for. Log it and keep going; assets already
                // registered above (procedural meshes processed earlier in this
                // loop) remain usable either way.
                std::cout << "[catalog] could not decode model " << declared.id << " from Asset Pack " << packId
                          << "; continuing without it." << std::endl;
                continue;
            }
            SetSourceIdentity(cacheKey, declared.id, {}, packId, version);
            if (Find(declared.id).mesh == nullptr)
                AddAlias(declared.id, cacheKey);
        }
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    loadedPacks_.insert(key);
    std::cout << "[catalog] loaded Asset Pack " << packId << " " << version << std::endl;
    return true;
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
    assets_[name.toStdString()] = Asset{ name, {}, {}, {}, mesh, material, skeleton, animationClips };
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
    assets_[name.toStdString()] = Asset{ name, {}, {}, {}, std::move(mesh), std::move(material) };
    return true;
}

bool AssetCatalog::AddAlias(const juce::String& durableAssetId, const juce::String& loadedAssetName)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto source = assets_.find(loadedAssetName.toStdString());
    if (durableAssetId.isEmpty() || source == assets_.end()) return false;
    assets_[durableAssetId.toStdString()] = source->second;
    return true;
}

bool AssetCatalog::SetSourceIdentity(const juce::String& cacheKey, const juce::String& assetId,
                                     const juce::String& versionId, const juce::String& packId,
                                     const juce::String& packVersion)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto entry = assets_.find(cacheKey.toStdString());
    if (entry == assets_.end() || assetId.isEmpty()) return false;
    entry->second.assetId = assetId;
    entry->second.versionId = versionId;
    entry->second.packId = packId;
    entry->second.packVersion = packVersion;
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
