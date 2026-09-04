#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <JuceHeader.h>

#include "creation/assets/VirtualFileSystem.h"
#include "Render/GL/Texture2D.h"
#include "Render/Scene/Animation.h"
#include "Render/Scene/Vertex.h"

namespace ce {

// One glTF mesh primitive as plain CPU-side geometry, ready for
// Mesh::Upload. Only triangle-list primitives with POSITION/NORMAL/
// TEXCOORD_0 are handled — morph targets and non-triangle topologies are
// still deferred. JOINTS_0/WEIGHTS_0 are read into Vertex's
// boneIndices/boneWeights when present (AI4) — see LoadedSkin below for
// the bone hierarchy those indices refer to.
struct LoadedPrimitive {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    int materialIndex = -1; // index into LoadedModel::materials, -1 if none.
};

// One joint in a skin's bind pose. Everything a scene::Skeleton component
// needs to both skin a mesh (inverseBindMatrix) and reconstruct the
// hierarchy at any pose, bind or animated (parentIndex +
// localBindTransform) — AI4 only ever displays the bind pose, but AI5's
// animation playback needs the same hierarchy walked with animated local
// transforms instead of these bind ones, so it's captured now rather
// than re-parsing the glTF a second time later.
struct LoadedJoint {
    juce::String name;
    int parentIndex = -1; // index into LoadedSkin::joints, or -1 for a root joint.
    juce::Matrix3D<float> inverseBindMatrix; // mesh-space -> this joint's space, at bind pose.
    juce::Matrix3D<float> localBindTransform; // this joint's transform relative to its parent, at bind pose.

    // Same bind pose as localBindTransform above, but decomposed into TRS
    // rather than pre-composed -- AI5's animation channels only ever
    // animate translation/rotation/scale individually (never "the whole
    // matrix"), so a channel that animates just this joint's rotation
    // needs a real translation/scale default to fall back to, not an
    // identity one. Read straight from cgltf_node's own translation/
    // rotation/scale fields, which cgltf populates with the correct
    // glTF-spec defaults even for nodes that don't explicitly author
    // them -- see ExtractSkin in the .cpp for the one documented case
    // (a node authored with a raw matrix instead of TRS) where this
    // doesn't reconstruct the bind pose exactly.
    juce::Vector3D<float> bindTranslation;
    float bindRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // x, y, z, w.
    juce::Vector3D<float> bindScale{ 1.0f, 1.0f, 1.0f };
};

// One glTF skin, flattened into a cache-friendly array (see LoadedJoint)
// instead of the glTF node-graph representation cgltf hands back. Only
// the first skin actually referenced by a mesh in the file is extracted
// — multi-skin files are rare and out of scope for this pass.
struct LoadedSkin {
    std::vector<LoadedJoint> joints;
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

// One node in the glTF file's scene graph, flattened the same way
// LoadedJoint flattens a skin's joints -- except this covers EVERY node
// in the file (not just skinned ones), so parentIndex is always
// resolvable when the node has a real parent (no ExtractSkin-style
// "external parent treated as root" limitation; see GltfLoader.cpp).
// Feeds a multi-part model's decomposition into Object Definition
// components (docs/OBJECT_MODEL.md) -- one LoadedNode with meshIndex >= 0
// becomes one Mesh-kind component, positioned by this node's own local
// transform relative to its parent.
struct LoadedNode {
    juce::String name;
    int parentIndex = -1; // index into LoadedModel::nodes; -1 only for a true scene root.

    // This node's transform relative to its immediate parent (or scene-
    // root space if parentIndex == -1). Decomposed to Euler, not left as
    // a quaternion (unlike LoadedJoint::bindRotation) -- this feeds
    // engine::Transform (Source/Scene/ObjectDefinitions.h), which has no
    // quaternion field.
    juce::Vector3D<float> localTranslation;
    juce::Vector3D<float> localEulerRotationRadians;
    juce::Vector3D<float> localScale{ 1.0f, 1.0f, 1.0f };

    int meshIndex = -1; // index into LoadedModel::meshPrimitiveRanges, -1 if this node has no mesh.
};

struct LoadedModel {
    std::vector<LoadedPrimitive> primitives;
    std::vector<LoadedMaterial> materials;
    std::optional<LoadedSkin> skin; // set only if some mesh in the file is actually skinned.

    // AI5: animation clips whose channels target a joint in `skin` above
    // -- only extracted when a skin exists, and only channels landing on
    // one of its joints are kept (a channel targeting some other node,
    // e.g. root motion or a camera, is out of scope for this pass and is
    // skipped). AnimationChannel::jointIndex indexes into skin->joints /
    // the resulting scene::Skeleton::joints the same way.
    std::vector<AnimationClip> animations;

    // One [first, first+count) span into `primitives` per glTF mesh, in
    // the exact order the primitive-building loop already produces --
    // lets LoadedNode::meshIndex map to "which primitives" without
    // changing how `primitives` itself is built or ordered.
    struct MeshPrimitiveRange { std::size_t first = 0; std::size_t count = 0; };
    std::vector<MeshPrimitiveRange> meshPrimitiveRanges;

    // Every node in the file's scene graph -- see LoadedNode. Always
    // populated (single- and multi-mesh files alike); the caller decides
    // what to do with one node the same way it decides for many.
    std::vector<LoadedNode> nodes;
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
bool LoadGltfFromVfs(creation::assets::VirtualFileSystem& vfs, const juce::String& virtualGltfPath, LoadedModel& outModel);

} // namespace ce
