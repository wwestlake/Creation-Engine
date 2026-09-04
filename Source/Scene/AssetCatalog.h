#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <JuceHeader.h>

#include "Render/GL/Texture2D.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/Scene/Vertex.h"
#include "Scene/Components.h"

namespace creation::assets { class VirtualFileSystem; }
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
        // Durable source identity. GPU data is a cache; scenes store these
        // values, so a placed item can be resolved after reopening.
        juce::String assetId;
        juce::String versionId;
        juce::String packId;
        juce::String packVersion;
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        // AI4: null unless the source model was skinned. Shared (not
        // copied) across every placed instance of this asset the same
        // way mesh/material already are -- the bind pose itself never
        // differs per instance, only future per-instance animation
        // playback state (AI5) would.
        std::shared_ptr<Skeleton> skeleton;

        // AI5: null unless the source model carried at least one
        // animation clip targeting `skeleton`'s joints. Shared across
        // instances for the same reason skeleton is -- clip DATA is
        // asset-level, only a placed entity's scene::Animator (playback
        // position/state) is per-instance.
        std::shared_ptr<std::vector<AnimationClip>> animationClips;
    };

    // Loads the built-in primitives (always available) and optional bundled
    // demo content from the project VFS when present.
    void LoadBuiltins(creation::assets::VirtualFileSystem& vfs);

    // Resolves declared pack assets into the GL cache. The exact VFS pack
    // version is runtime authority; a local decoder cache is temporary only.
    bool LoadAssetPack(const juce::String& packId, const juce::String& version, juce::String& errorMessage);
    [[nodiscard]] static juce::String PackAssetKey(const juce::String& packId,
                                                    const juce::String& version,
                                                    const juce::String& assetId);

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
    bool AddFromModel(const juce::String& name, const LoadedModel& model, creation::assets::VirtualFileSystem* vfs = nullptr);

    // Same as AddFromModel, but builds from ONE mesh-bearing node of a
    // multi-part model instead of always the file's first primitive --
    // see docs/OBJECT_MODEL.md's "Multi-part import decomposes into
    // components". nodeIndex indexes LoadedModel::nodes; the node's own
    // meshIndex resolves which primitive range (LoadedModel::
    // meshPrimitiveRanges) to build from. Returns false if nodeIndex is
    // out of range or that node has no mesh.
    bool AddNodeFromModel(const juce::String& name, const LoadedModel& model, int nodeIndex,
                          creation::assets::VirtualFileSystem* vfs = nullptr);

    // Runtime cache key for one part of a multi-part durable asset --
    // AddNodeFromModel/Find callers use this instead of the bare assetId
    // whenever nodeIndex >= 0 (see MeshAssetReference::nodeIndex).
    [[nodiscard]] static juce::String NodeAssetKey(const juce::String& assetId, const juce::String& versionId,
                                                    int nodeIndex);

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

    // Maps a durable VFS asset ID to an already-loaded catalog entry without
    // adding a second row to the authoring UI. The live catalog remains a GPU
    // cache; scene serialization uses the durable ID.
    bool AddAlias(const juce::String& durableAssetId, const juce::String& loadedAssetName);

    // Assigns the durable source identity after a loader has populated the
    // GPU cache entry. Used by VFS packs and project-content importers.
    bool SetSourceIdentity(const juce::String& cacheKey, const juce::String& assetId,
                           const juce::String& versionId = {}, const juce::String& packId = {},
                           const juce::String& packVersion = {});

    // Returns a copy (two shared_ptr refcount bumps, cheap) rather than a
    // pointer/reference into assets_ -- a concurrent AddFromModel() call
    // re-importing the same name would otherwise be free to overwrite the
    // very Asset a caller on another thread is still reading through a
    // pointer to. Asset{} (mesh == nullptr) means "not found".
    Asset Find(const juce::String& name) const;
    std::vector<juce::String> Names() const;

    // Evicts a mesh asset from the runtime GPU cache -- frees this
    // catalog's own mesh/material/skeleton/texture references. An entity
    // already placed from this asset (Find() returns a copy of the
    // shared_ptrs, not a reference into assets_) keeps its own copies
    // alive regardless; this only stops the asset from being findable/
    // placeable/browsable going forward. Callers are responsible for any
    // dependency check before calling this -- Remove() itself is
    // unconditional, same as Add()/AddFromModel() do no such check either.
    // Returns false if name isn't a registered asset.
    bool Remove(const juce::String& name);

    // Materials are their own named, independent registry -- NOT a value
    // owned by whichever mesh asset happens to reference one. A Material
    // can be edited (MaterialGraphPanel) with no mesh asset in the
    // picture at all, the same way opening a Material asset in a content
    // browser never requires selecting an object first. A mesh asset's
    // own Asset::material field is a SLOT: a shared_ptr reference into
    // this registry, not an embedded, privately-owned instance -- see
    // AssignMaterial below for repointing a slot to a different named
    // material.
    //
    // Returns the SAME shared_ptr on every call for a given name (never
    // rebuilds it) -- MaterialGraphPanel calls this once per compile and
    // mutates the returned object's fields in place, so every mesh slot
    // already pointing at this name sees the update through the shared
    // instance without needing to re-resolve anything.
    std::shared_ptr<Material> GetOrCreateMaterial(const juce::String& name);
    [[nodiscard]] std::shared_ptr<Material> FindMaterial(const juce::String& name) const;
    [[nodiscard]] std::vector<juce::String> MaterialNames() const;

    // Evicts a material from the registry. Same "runtime cache only, no
    // dependency check" contract as Remove() above -- a mesh asset slot
    // still pointing at this name (AssignMaterial) keeps its own
    // shared_ptr alive; it just stops being resolvable by name for any
    // *new* AssignMaterial/GetOrCreateMaterial call. Returns false if
    // name isn't a registered material.
    bool RemoveMaterial(const juce::String& name);

    // Repoints a mesh asset's material SLOT to reference a different
    // named material (creating that material, empty, if it doesn't exist
    // yet) -- the actual "assign this material to this object" action,
    // kept deliberately separate from editing/compiling a material
    // itself. Returns false if meshAssetName isn't a registered mesh
    // asset.
    bool AssignMaterial(const juce::String& meshAssetName, const juce::String& materialName);

    // A bare texture, independent of any mesh/material -- for a Texture
    // Sample node's own reference (ce::material::CompileMaterialGraph's
    // texture slots). Keyed by absolute file path since there's no
    // asset-picker/import step yet (see material_nodes.cpp's Texture
    // Sample description): loading the same path twice returns the same
    // cached instance rather than re-uploading it. Must be called with a
    // current GL context, same as Add()/AddFromModel() above -- callers
    // off the render thread need to hop via
    // OpenGLContext::executeOnGLThread first. Returns nullptr if the file
    // doesn't decode as an image.
    std::shared_ptr<gl::Texture2D> GetOrLoadTexture(const juce::File& file);

private:
    void AddProcedural(const juce::String& name, const std::vector<Vertex>& vertices,
                        const std::vector<GLuint>& indices, juce::Vector3D<float> albedo);

    // Shared body of AddFromModel/AddNodeFromModel -- builds mesh/
    // material/texture/skeleton/animations from ONE primitive of an
    // already-parsed model and registers the result under `name`.
    bool BuildAssetFromPrimitive(const juce::String& name, const LoadedModel& model, std::size_t primitiveIndex,
                                 creation::assets::VirtualFileSystem* vfs);

    std::unordered_set<std::string> loadedPacks_;
    mutable std::mutex mutex_;
    // Keyed by the owning asset's name so Remove() can free exactly this
    // asset's texture -- was an unkeyed vector before Remove() existed,
    // since nothing needed to find one again by name until now.
    std::unordered_map<std::string, std::unique_ptr<gl::Texture2D>> ownedTextures_;
    std::unordered_map<std::string, Asset> assets_;
    std::vector<juce::String> names_;
    std::unordered_map<std::string, std::shared_ptr<Material>> materials_;
    std::unordered_map<std::string, std::shared_ptr<gl::Texture2D>> loadedTextures_;
};

} // namespace ce::scene
