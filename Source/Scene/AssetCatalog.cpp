#include "Scene/AssetCatalog.h"

#include <algorithm>
#include <iostream>

#include <creation/material/material_compiler.h>
#include <creation/material/material_nodes.h>
#include <node_system/frgraph_serialization.h>
#include <node_system/graph.h>
#include <node_system/type_registry.h>

#include "Assets/AssetPackStore.h"
#include "Render/Import/GltfLoader.h"
#include "Render/Scene/ProceduralMesh.h"

namespace ce::scene {

namespace {

node_system::Pin* FindInputPin(node_system::Node& node, const char* name) {
    const auto it = std::find_if(node.Inputs().begin(), node.Inputs().end(),
                                  [&](const node_system::Pin& pin) { return pin.name == name; });
    return it == node.Inputs().end() ? nullptr : node.FindPin(it->id);
}

// One Texture Sample node, its "texture" input set to the given absolute
// disk path -- the same convention MaterialGraphPanel's hand-authored
// graphs already use (material_nodes.cpp's own doc: "texture is an
// absolute file path, resolved into a real GPU texture when the graph
// compiles").
node_system::Node* AddTextureSampleNode(node_system::Graph& graph, const node_system::NodeTypeRegistry& registry,
                                         const juce::String& texturePath) {
    auto* node = node_system::AddRegisteredNode(graph, registry, "material.texture.sample2d");
    if (node == nullptr) return nullptr;
    if (auto* texturePin = FindInputPin(*node, "texture")) texturePin->defaultValue = texturePath.toStdString();
    return node;
}

// glTF import's answer to "the importer can just build the material
// itself" -- a real Material Graph (the same data model
// MaterialGraphPanel's hand-authored graphs use), generated directly from
// whatever texture maps the source file actually has, then compiled and
// bound exactly the way MaterialGraphPanel::compileAndSave already does.
// No human has to open the graph editor for the common "here's a base
// color texture, maybe a metallic-roughness texture" case -- this IS that
// graph, authored programmatically instead of by hand.
//
// Normal maps are intentionally NOT wired to Material Output's normal
// input here (see LoadedMaterial::normalTexturePath's own comment,
// GltfLoader.h) -- the compiled-graph pipeline has no tangent basis to
// decode a tangent-space sample into a valid world-space normal yet, so a
// Texture Sample node is still generated (present and ready once that
// support exists) but left unconnected rather than wired to something
// that would render visibly wrong.
void ApplyGeneratedMaterialGraph(Material& material, const LoadedMaterial& srcMaterial, AssetCatalog& catalog) {
    node_system::NodeTypeRegistry registry;
    material::RegisterMaterialNodes(registry);
    node_system::Graph graph("Imported Material", node_system::GraphTarget::Material);

    auto* outputNode = node_system::AddRegisteredNode(graph, registry, "material.surface.output");
    if (outputNode == nullptr) return;

    if (srcMaterial.baseColorTexturePath.existsAsFile()) {
        if (auto* sampleNode = AddTextureSampleNode(graph, registry, srcMaterial.baseColorTexturePath.getFullPathName())) {
            if (auto* baseColorInput = FindInputPin(*outputNode, "baseColor"))
                graph.Connect(sampleNode->Id(), sampleNode->Outputs().front().id, outputNode->Id(), baseColorInput->id);
        }
    }

    if (srcMaterial.metallicRoughnessTexturePath.existsAsFile()) {
        // glTF spec, "Metal-Roughness Material": G channel = roughness,
        // B channel = metallic -- the same texture packed both ways every
        // common export pipeline (Blender, Substance) already uses.
        if (auto* sampleNode =
                AddTextureSampleNode(graph, registry, srcMaterial.metallicRoughnessTexturePath.getFullPathName())) {
            auto addMask = [&](const char* channel, const char* outputInputName) {
                auto* maskNode = node_system::AddRegisteredNode(graph, registry, "material.componentmask");
                if (maskNode == nullptr) return;
                if (auto* channelPin = FindInputPin(*maskNode, "channel")) channelPin->defaultValue = std::string(channel);
                if (auto* valuePin = FindInputPin(*maskNode, "value"))
                    graph.Connect(sampleNode->Id(), sampleNode->Outputs().front().id, maskNode->Id(), valuePin->id);
                if (auto* outputInput = FindInputPin(*outputNode, outputInputName))
                    graph.Connect(maskNode->Id(), maskNode->Outputs().front().id, outputNode->Id(), outputInput->id);
            };
            addMask("g", "roughness");
            addMask("b", "metallic");
        }
    }

    if (srcMaterial.normalTexturePath.existsAsFile()) {
        AddTextureSampleNode(graph, registry, srcMaterial.normalTexturePath.getFullPathName());
    }

    const auto compileResult = material::CompileMaterialGraph(graph, registry);
    if (!compileResult.ok) {
        // A generated graph failing to compile is a real bug in this
        // function, not a user-facing condition -- fall back to the
        // factor-only flat material (already set by the caller) rather
        // than leaving the entity with no material at all.
        std::cout << "[catalog] generated material graph failed to compile; using flat material instead." << std::endl;
        for (const auto& err : compileResult.errors) std::cout << "  " << err << std::endl;
        return;
    }

    material.compiledMaterialSource = juce::String(compileResult.source.declarations) + "\n" +
                                       juce::String(compileResult.source.evaluateFunction) + "\n" +
                                       juce::String(compileResult.source.vertexFunction);
    material.savedGraphSource = node_system::SerializeGraph(graph);
    material.textureBindings.clear();
    for (const auto& tex : compileResult.source.textures) {
        if (auto texture = catalog.GetOrLoadTexture(juce::File(tex.path))) {
            material.textureBindings[tex.uniformName] = texture;
        }
    }
}

} // namespace

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
        else if (declared.kind == "model" || declared.kind == "character")
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
            // Register each mesh-bearing source node as its own GPU entry and
            // retain the source hierarchy. A packed character is a complete
            // authored object, not an implicit alias for primitive zero.
            ModelHierarchy hierarchy;
            hierarchy.nodes.reserve(model.nodes.size());
            for (std::size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
                const auto& sourceNode = model.nodes[nodeIndex];
                ModelHierarchyNode node;
                node.sourceNodeIndex = static_cast<int>(nodeIndex);
                node.parentIndex = sourceNode.parentIndex;
                node.localTransform.position = { sourceNode.localTranslation.x, sourceNode.localTranslation.y,
                                                 sourceNode.localTranslation.z };
                node.localTransform.eulerRotationRadians = { sourceNode.localEulerRotationRadians.x,
                                                              sourceNode.localEulerRotationRadians.y,
                                                              sourceNode.localEulerRotationRadians.z };
                node.localTransform.scale = { sourceNode.localScale.x, sourceNode.localScale.y, sourceNode.localScale.z };
                node.hasMesh = sourceNode.meshIndex >= 0;
                hierarchy.nodes.push_back(node);
                if (!node.hasMesh) continue;

                const auto nodeKey = NodeAssetKey(cacheKey, {}, static_cast<int>(nodeIndex));
                if (!AddNodeFromModel(nodeKey, model, static_cast<int>(nodeIndex))) {
                    errorMessage = "Could not build mesh node " + juce::String(static_cast<int>(nodeIndex)) +
                                   " for " + declared.id + ".";
                    return false;
                }
                SetSourceIdentity(nodeKey, declared.id, {}, packId, version);
            }
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                modelHierarchies_[cacheKey.toStdString()] = std::move(hierarchy);
            }
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
    return BuildAssetFromPrimitive(name, model, 0, vfs);
}

bool AssetCatalog::AddNodeFromModel(const juce::String& name, const LoadedModel& model, int nodeIndex,
                                    creation::assets::VirtualFileSystem* vfs) {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= model.nodes.size()) {
        return false;
    }
    const auto& node = model.nodes[static_cast<std::size_t>(nodeIndex)];
    if (node.meshIndex < 0 || static_cast<std::size_t>(node.meshIndex) >= model.meshPrimitiveRanges.size()) {
        return false;
    }
    const auto& range = model.meshPrimitiveRanges[static_cast<std::size_t>(node.meshIndex)];
    if (range.count == 0) {
        return false;
    }
    return BuildAssetFromPrimitive(name, model, range.first, vfs);
}

juce::String AssetCatalog::NodeAssetKey(const juce::String& assetId, const juce::String& versionId, int nodeIndex) {
    return assetId + "@" + versionId + "#node" + juce::String(nodeIndex);
}

std::optional<AssetCatalog::ModelHierarchy> AssetCatalog::FindModelHierarchy(const juce::String& name) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = modelHierarchies_.find(name.toStdString());
    if (found == modelHierarchies_.end()) return std::nullopt;
    return found->second;
}

bool AssetCatalog::BuildAssetFromPrimitive(const juce::String& name, const LoadedModel& model, std::size_t primitiveIndex,
                                           creation::assets::VirtualFileSystem* vfs) {
    if (primitiveIndex >= model.primitives.size()) {
        return false;
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->Upload(model.primitives[primitiveIndex].vertices, model.primitives[primitiveIndex].indices);

    auto material = std::make_shared<Material>();
    std::unique_ptr<gl::Texture2D> texture;
    const int materialIndex = model.primitives[primitiveIndex].materialIndex;
    if (materialIndex >= 0 && materialIndex < static_cast<int>(model.materials.size())) {
        const auto& srcMaterial = model.materials[static_cast<std::size_t>(materialIndex)];
        material->albedo = srcMaterial.baseColorFactor;
        material->metallic = srcMaterial.metallicFactor;
        material->roughness = srcMaterial.roughnessFactor;

        const bool hasAnyDiskTexture = vfs == nullptr &&
            (srcMaterial.baseColorTexturePath.existsAsFile() || srcMaterial.metallicRoughnessTexturePath.existsAsFile() ||
             srcMaterial.normalTexturePath.existsAsFile());

        if (hasAnyDiskTexture) {
            // A real Material Graph, generated on the spot from whichever
            // texture maps this material actually has -- see
            // ApplyGeneratedMaterialGraph's own comment above. Replaces
            // (rather than supplements) the fixed albedo/albedoTexture
            // path below: Material::Resolve() picks one or the other
            // based on compiledMaterialSource being set, never both.
            ApplyGeneratedMaterialGraph(*material, srcMaterial, *this);
        } else if (vfs != nullptr && srcMaterial.baseColorTextureVirtualPath.isNotEmpty()) {
            juce::MemoryBlock textureBytes;
            if (vfs->readFile(srcMaterial.baseColorTextureVirtualPath, textureBytes)) {
                texture = std::make_unique<gl::Texture2D>();
                if (texture->LoadFromMemory(textureBytes.getData(), textureBytes.getSize(),
                                             srcMaterial.baseColorTextureVirtualPath)) {
                    material->albedoTexture = texture.get();
                }
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
        ownedTextures_[name.toStdString()] = std::move(texture);
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
        ownedTextures_[name.toStdString()] = std::move(ownedTexture);
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

bool AssetCatalog::Remove(const juce::String& name) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto key = name.toStdString();
    if (assets_.erase(key) == 0) return false;
    ownedTextures_.erase(key); // no-op if this asset never owned a texture of its own.
    names_.erase(std::remove(names_.begin(), names_.end(), name), names_.end());
    return true;
}

std::shared_ptr<Material> AssetCatalog::GetOrCreateMaterial(const juce::String& name) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = materials_[name.toStdString()];
    if (slot == nullptr) slot = std::make_shared<Material>();
    return slot;
}

std::shared_ptr<Material> AssetCatalog::FindMaterial(const juce::String& name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = materials_.find(name.toStdString());
    return it == materials_.end() ? nullptr : it->second;
}

std::vector<juce::String> AssetCatalog::MaterialNames() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<juce::String> result;
    result.reserve(materials_.size());
    for (const auto& [name, material] : materials_) result.push_back(name);
    return result;
}

bool AssetCatalog::RemoveMaterial(const juce::String& name) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return materials_.erase(name.toStdString()) != 0;
}

std::shared_ptr<gl::Texture2D> AssetCatalog::GetOrLoadTexture(const juce::File& file) {
    const auto key = file.getFullPathName().toStdString();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = loadedTextures_.find(key); it != loadedTextures_.end()) return it->second;
    }

    auto texture = std::make_shared<gl::Texture2D>();
    if (!texture->LoadFromFile(file)) return nullptr;

    const std::lock_guard<std::mutex> lock(mutex_);
    // Another thread could have loaded the same path while this one was
    // decoding -- keep whichever landed first rather than uploading twice.
    auto& slot = loadedTextures_[key];
    if (slot == nullptr) slot = std::move(texture);
    return slot;
}

bool AssetCatalog::AssignMaterial(const juce::String& meshAssetName, const juce::String& materialName) {
    std::shared_ptr<Material> material;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto assetIt = assets_.find(meshAssetName.toStdString());
        if (assetIt == assets_.end()) return false;

        auto& materialSlot = materials_[materialName.toStdString()];
        if (materialSlot == nullptr) materialSlot = std::make_shared<Material>();
        material = materialSlot;
        assetIt->second.material = material;
    }
    return true;
}

} // namespace ce::scene
