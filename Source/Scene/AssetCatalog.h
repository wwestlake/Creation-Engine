#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>

#include "assets/VirtualFileSystem.h"
#include "Render/GL/Texture2D.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/Scene/Vertex.h"
#include "Scene/Components.h"

namespace ce {
struct LoadedModel; // Render/Import/GltfLoader.h
}

namespace ce::scene {

// Owns GPU-resident mesh/material/texture assets behind shared_ptr, so
// multiple placed entities can reference the same asset without
// duplicating GPU resources. Lifetime is tied to the GL context — build
// it in newOpenGLContextCreated, drop it in openGLContextClosing, same
// rule every other GL-resource owner in this render layer follows.
//
// Building a Mesh needs a live GL context (vertex/index buffer upload);
// building a Material does not (it only becomes a real shader program
// lazily, the first time something calls Material::Resolve() at draw
// time) — but both happen here together for one asset at a time, so
// callers never have to know which part needs the context and which
// doesn't.
//
// Was safe to read unlocked back when LoadBuiltins() was the only thing
// that ever wrote to it, all before the app did anything else with the
// catalog. AddFromModel() (Source/Import's importers) can now add a new
// asset from the render thread at essentially any time while the message
// thread is concurrently reading Find()/Names() (the "+ Add" menu) — a
// real, exercised race on assets_/names_ without mutex_, not a
// hypothetical. Names() returns by value rather than by reference for
// the same reason ViewportComponent's light getters do: a reference
// handed across the lock boundary could be invalidated the moment a
// concurrent AddFromModel() reallocates names_'s buffer.
class AssetCatalog final {
public:
    struct Asset {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        // AI4: null unless the source model was skinned. Shared (not
        // copied) across every placed instance of this asset the same
        // way mesh/material already are -- the bind pose itself never
        // differs per instance, only future per-instance animation
        // playback state (AI5) would.
        std::shared_ptr<Skeleton> skeleton;
    };

    // Loads the built-in demo set: a procedural cube, a procedural
    // sphere, and the BoxTextured glTF asset read through vfs (expects
    // assets/packages/base.zip already mounted).
    void LoadBuiltins(assets::VirtualFileSystem& vfs);

    // Builds a Mesh/Material (and texture/Skeleton, if the model has
    // them) from already-parsed glTF data and registers it under `name`,
    // overwriting any existing asset with that name. Only the first
    // primitive is used, same limitation LoadBuiltins()' BoxTextured
    // loading already has. Pass `vfs` when model's textures were
    // resolved through it
    // (LoadGltfFromVfs populated LoadedMaterial::baseColorTextureVirtualPath);
    // leave it null for disk-resolved models (LoadGltf populated
    // baseColorTexturePath instead) -- mirrors LoadGltf/LoadGltfFromVfs's
    // own disk-vs-VFS duality. Must be called with a current GL context
    // (Mesh::Upload/Texture2D need one) -- callers off the render thread
    // need to hop via OpenGLContext::executeOnGLThread first.
    bool AddFromModel(const juce::String& name, const LoadedModel& model, assets::VirtualFileSystem* vfs = nullptr);

    // Registers an already-built mesh/material pair under `name`,
    // overwriting any existing asset with that name -- the generic
    // primitive AddProcedural/AddFromModel are themselves effectively
    // built on top of. For an importer that just needs "here's a Mesh
    // and Material, please track them" (e.g. TextureAssetImporter, which
    // reuses a stock cube mesh rather than parsing one), this skips the
    // model-parsing-specific machinery those two carry. Pass
    // ownedTexture when the caller built a new Texture2D for this asset
    // -- ownership transfers to the catalog (same as AddFromModel's own
    // textures), and material->albedoTexture should already point at it.
    bool Add(const juce::String& name, std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material,
             std::unique_ptr<gl::Texture2D> ownedTexture = nullptr);

    // Returns a copy (two shared_ptr refcount bumps, cheap) rather than a
    // pointer/reference into assets_ -- a concurrent AddFromModel() call
    // re-importing the same name would otherwise be free to overwrite the
    // very Asset a caller on another thread is still reading through a
    // pointer to. Asset{} (mesh == nullptr) means "not found".
    Asset Find(const juce::String& name) const;
    std::vector<juce::String> Names() const;

private:
    void AddProcedural(const juce::String& name, const std::vector<Vertex>& vertices,
                        const std::vector<GLuint>& indices, juce::Vector3D<float> albedo);

    bool hasLoadedBuiltins_ = false;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<gl::Texture2D>> ownedTextures_;
    std::unordered_map<std::string, Asset> assets_;
    std::vector<juce::String> names_;
};

} // namespace ce::scene
