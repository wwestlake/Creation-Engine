#pragma once

#include <memory>
#include <vector>

#include <JuceHeader.h>

#include "assets/VirtualFileSystem.h"
#include "Render/GL/Texture2D.h"
#include "Render/Scene/Vertex.h"

namespace ce {

// One glTF mesh primitive as plain CPU-side geometry, ready for
// Mesh::Upload. Only triangle-list primitives with POSITION/NORMAL/
// TEXCOORD_0 are handled — the rest of the M4 scope (skinning, morph
// targets, non-triangle topologies) is explicitly deferred.
struct LoadedPrimitive {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    int materialIndex = -1; // index into LoadedModel::materials, -1 if none.
};

// A glTF material's PBR metallic-roughness inputs, translated into the
// same terms Material (Source/Render/Scene/Material.h) already uses.
//
// Exactly one of baseColorTexturePath / baseColorTextureVirtualPath is
// ever populated, matching whichever of LoadGltf/LoadGltfFromVfs was
// used — mirrors ShaderComposer's disk-vs-VFS duality rather than
// picking one representation and losing information the other mode
// needs.
struct LoadedMaterial {
    juce::Vector3D<float> baseColorFactor{ 1.0f, 1.0f, 1.0f };
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    juce::File baseColorTexturePath;          // set by LoadGltf (disk mode); invalid if unset.
    juce::String baseColorTextureVirtualPath; // set by LoadGltfFromVfs (VFS mode); empty if unset.
};

struct LoadedModel {
    std::vector<LoadedPrimitive> primitives;
    std::vector<LoadedMaterial> materials;
};

// Parses gltfFile (.gltf with a sibling .bin, or .glb) via cgltf and
// extracts static mesh + PBR material data. Only external image URIs are
// resolved for textures right now — base64-embedded or buffer_view-
// embedded images are logged and skipped, not yet supported. Returns
// false (and logs why) on failure.
bool LoadGltf(const juce::File& gltfFile, LoadedModel& outModel);

// Same extraction, but the .gltf/.glb bytes — and any externally
// referenced .bin buffer — are read from a mounted VirtualFileSystem
// archive instead of disk. virtualGltfPath is the mount-relative path to
// the entry glTF file, e.g. "models/BoxTextured/BoxTextured.gltf";
// buffer URIs resolve relative to it the same way disk-mode resolves
// relative to gltfFile's parent directory.
bool LoadGltfFromVfs(assets::VirtualFileSystem& vfs, const juce::String& virtualGltfPath, LoadedModel& outModel);

} // namespace ce
