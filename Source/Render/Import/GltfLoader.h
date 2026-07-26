#pragma once

#include <memory>
#include <vector>

#include <JuceHeader.h>

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
struct LoadedMaterial {
    juce::Vector3D<float> baseColorFactor{ 1.0f, 1.0f, 1.0f };
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    juce::File baseColorTexturePath; // invalid/empty if the material has no base color texture.
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

} // namespace ce
