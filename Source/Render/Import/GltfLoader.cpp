#include "Render/Import/GltfLoader.h"

#include <cmath>
#include <cstring>
#include <array>
#include <unordered_map>
#include <utility>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "Diagnostics/EngineLog.h"

namespace ce {

namespace {

const cgltf_accessor* FindAttributeAccessor(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type) {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

struct NodeTransform {
    juce::Matrix3D<float> matrix;
    juce::Vector3D<float> translation;
    std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
    juce::Vector3D<float> scale{ 1.0f, 1.0f, 1.0f };
};

std::array<float, 4> QuaternionFromRotationMatrix(const float* matrix) {
    const float m00 = matrix[0], m01 = matrix[4], m02 = matrix[8];
    const float m10 = matrix[1], m11 = matrix[5], m12 = matrix[9];
    const float m20 = matrix[2], m21 = matrix[6], m22 = matrix[10];
    const float trace = m00 + m11 + m22;
    std::array<float, 4> rotation;

    if (trace > 0.0f) {
        const float scale = std::sqrt(trace + 1.0f) * 2.0f;
        rotation = { (m21 - m12) / scale, (m02 - m20) / scale, (m10 - m01) / scale, 0.25f * scale };
    } else if (m00 > m11 && m00 > m22) {
        const float scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        rotation = { 0.25f * scale, (m01 + m10) / scale, (m02 + m20) / scale, (m21 - m12) / scale };
    } else if (m11 > m22) {
        const float scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        rotation = { (m01 + m10) / scale, 0.25f * scale, (m12 + m21) / scale, (m02 - m20) / scale };
    } else {
        const float scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        rotation = { (m02 + m20) / scale, (m12 + m21) / scale, 0.25f * scale, (m10 - m01) / scale };
    }
    return rotation;
}

float ColumnLength(const float* matrix, int columnStart) {
    return std::sqrt(matrix[columnStart] * matrix[columnStart]
                   + matrix[columnStart + 1] * matrix[columnStart + 1]
                   + matrix[columnStart + 2] * matrix[columnStart + 2]);
}

NodeTransform ReadNodeTransform(const cgltf_node& node, bool preserveScale) {
    float matrix[16];
    cgltf_node_transform_local(&node, matrix);

    float rotationMatrix[16];
    std::memcpy(rotationMatrix, matrix, sizeof(rotationMatrix));

    const float scaleX = ColumnLength(matrix, 0);
    const float scaleY = ColumnLength(matrix, 4);
    const float scaleZ = ColumnLength(matrix, 8);

    const auto normalizeColumn = [](float* values, int columnStart, float length) {
        if (length > 0.0f) {
            values[columnStart] /= length;
            values[columnStart + 1] /= length;
            values[columnStart + 2] /= length;
        }
    };
    normalizeColumn(rotationMatrix, 0, scaleX);
    normalizeColumn(rotationMatrix, 4, scaleY);
    normalizeColumn(rotationMatrix, 8, scaleZ);

    NodeTransform result;
    result.matrix = juce::Matrix3D<float>(preserveScale ? matrix : rotationMatrix);
    result.translation = { matrix[12], matrix[13], matrix[14] };
    result.rotation = QuaternionFromRotationMatrix(rotationMatrix);
    result.scale = preserveScale ? juce::Vector3D<float>{ scaleX, scaleY, scaleZ }
                                 : juce::Vector3D<float>{ 1.0f, 1.0f, 1.0f };
    return result;
}

NodeTransform NodeTransformIgnoringScale(const cgltf_node& node) {
    return ReadNodeTransform(node, false);
}

NodeTransform NodeTransformPreservingScale(const cgltf_node& node) {
    return ReadNodeTransform(node, true);
}

juce::Matrix3D<float> RootParentBindTransform(const cgltf_node& node,
                                              const std::unordered_map<const cgltf_node*, int>& jointIndexByNode) {
    std::vector<const cgltf_node*> ancestors;
    for (const cgltf_node* parent = node.parent; parent != nullptr; parent = parent->parent) {
        if (jointIndexByNode.find(parent) != jointIndexByNode.end()) {
            break;
        }
        ancestors.push_back(parent);
    }

    juce::Matrix3D<float> result;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        result = result * NodeTransformPreservingScale(**it).matrix;
    }
    return result;
}

bool IsLegacyBlenderConversionRoot(const cgltf_data& data, const cgltf_node& node) {
    // Blender's older MakeHuman/FBX path can emit metre-sized Z-up vertices
    // below one glTF conversion wrapper (+90 degrees around X, .1 scale).
    // This is source export baggage, not authored scene placement. Keep the
    // test deliberately narrow so a normal Blender scene root is untouched.
    if (data.asset.generator == nullptr || std::strstr(data.asset.generator, "Blender I/O") == nullptr ||
        node.parent != nullptr || node.has_rotation == 0 || node.has_scale == 0) {
        return false;
    }
    constexpr float epsilon = 0.0005f;
    const auto near = [](float left, float right) { return std::abs(left - right) < epsilon; };
    const float expected = std::sqrt(0.5f);
    return near(node.rotation[0], expected) && near(node.rotation[1], 0.0f) &&
           near(node.rotation[2], 0.0f) && near(node.rotation[3], expected) &&
           near(node.scale[0], 0.1f) && near(node.scale[1], 0.1f) && near(node.scale[2], 0.1f);
}

// Flattens one cgltf_skin's node-graph joints into LoadedSkin's
// cache-friendly array. If a skin root has authored non-joint ancestors,
// keep that ancestor chain on the root joint so the runtime palette remains
// in the same bind space as the inverse bind matrices.
LoadedSkin ExtractSkin(const cgltf_skin& skin) {
    LoadedSkin loadedSkin;
    loadedSkin.joints.reserve(skin.joints_count);

    std::unordered_map<const cgltf_node*, int> jointIndexByNode;
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        jointIndexByNode[skin.joints[j]] = static_cast<int>(j);
    }

    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        const cgltf_node* node = skin.joints[j];
        LoadedJoint joint;
        joint.name = node->name != nullptr ? juce::String(node->name) : ("Joint" + juce::String(static_cast<int>(j)));

        if (node->parent != nullptr) {
            const auto it = jointIndexByNode.find(node->parent);
            if (it != jointIndexByNode.end()) {
                joint.parentIndex = it->second;
            }
        }

        const auto nodeTransform = NodeTransformPreservingScale(*node);
        joint.localBindTransform = nodeTransform.matrix;
        if (joint.parentIndex < 0) {
            joint.rootParentBindTransform = RootParentBindTransform(*node, jointIndexByNode);
        }
        joint.bindTranslation = nodeTransform.translation;
        joint.bindRotation[0] = nodeTransform.rotation[0];
        joint.bindRotation[1] = nodeTransform.rotation[1];
        joint.bindRotation[2] = nodeTransform.rotation[2];
        joint.bindRotation[3] = nodeTransform.rotation[3];
        joint.bindScale = nodeTransform.scale;

        loadedSkin.joints.push_back(joint);
    }

    if (skin.inverse_bind_matrices != nullptr) {
        const auto count = juce::jmin(static_cast<std::size_t>(skin.inverse_bind_matrices->count),
                                      loadedSkin.joints.size());
        for (std::size_t index = 0; index < count; ++index) {
            float inverseBindMatrix[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, static_cast<cgltf_size>(index), inverseBindMatrix, 16);
            loadedSkin.joints[index].inverseBindMatrix = juce::Matrix3D<float>(inverseBindMatrix);
        }
    }

    return loadedSkin;
}

// Extracts every animation clip whose channels drive one of `skin`'s
// joints. A channel targeting a node outside the skin (root motion, a
// camera, ...) is logged and skipped -- root motion extraction is
// explicitly AI6 scope, not this pass. Channels targeting morph-target
// weights are skipped too (no morph target support yet).
void ExtractAnimations(const cgltf_data& data, const cgltf_skin* skin, LoadedModel& outModel) {
    if (skin == nullptr) {
        return;
    }

    std::unordered_map<const cgltf_node*, int> jointIndexByNode;
    for (cgltf_size j = 0; j < skin->joints_count; ++j) {
        jointIndexByNode[skin->joints[j]] = static_cast<int>(j);
    }

    for (cgltf_size a = 0; a < data.animations_count; ++a) {
        const cgltf_animation& anim = data.animations[a];
        AnimationClip clip;
        clip.name = anim.name != nullptr ? juce::String(anim.name) : ("Animation" + juce::String(static_cast<int>(a)));

        for (cgltf_size c = 0; c < anim.channels_count; ++c) {
            const cgltf_animation_channel& channel = anim.channels[c];
            if (channel.target_node == nullptr || channel.sampler == nullptr) {
                continue;
            }

            const auto jointIt = jointIndexByNode.find(channel.target_node);
            if (jointIt == jointIndexByNode.end()) {
                continue; // targets a node outside this skin -- not this pass's concern.
            }

            AnimationChannel loaded;
            loaded.jointIndex = jointIt->second;
            switch (channel.target_path) {
                case cgltf_animation_path_type_translation:
                    loaded.path = AnimationChannel::Path::Translation;
                    break;
                case cgltf_animation_path_type_rotation:
                    loaded.path = AnimationChannel::Path::Rotation;
                    break;
                case cgltf_animation_path_type_scale:
                    loaded.path = AnimationChannel::Path::Scale;
                    break;
                default:
                    continue; // weights (morph targets) -- not supported.
            }

            const cgltf_animation_sampler& sampler = *channel.sampler;
            if (sampler.input == nullptr || sampler.output == nullptr) {
                continue;
            }

            switch (sampler.interpolation) {
                case cgltf_interpolation_type_step:
                    loaded.interpolation = AnimationInterpolation::Step;
                    break;
                case cgltf_interpolation_type_cubic_spline:
                    loaded.interpolation = AnimationInterpolation::CubicSpline;
                    break;
                default:
                    loaded.interpolation = AnimationInterpolation::Linear;
                    break;
            }

            const cgltf_accessor& timesAccessor = *sampler.input;
            loaded.times.resize(timesAccessor.count);
            for (cgltf_size k = 0; k < timesAccessor.count; ++k) {
                cgltf_accessor_read_float(&timesAccessor, k, &loaded.times[k], 1);
            }
            if (!loaded.times.empty()) {
                clip.duration = juce::jmax(clip.duration, loaded.times.back());
            }

            const int componentsPerKey = loaded.path == AnimationChannel::Path::Rotation ? 4 : 3;
            const cgltf_accessor& valuesAccessor = *sampler.output;
            loaded.values.resize(valuesAccessor.count * static_cast<cgltf_size>(componentsPerKey));
            for (cgltf_size k = 0; k < valuesAccessor.count; ++k) {
                cgltf_accessor_read_float(&valuesAccessor, k, &loaded.values[k * static_cast<cgltf_size>(componentsPerKey)],
                                           componentsPerKey);
            }

            clip.channels.push_back(std::move(loaded));
        }

        if (!clip.channels.empty()) {
            outModel.animations.push_back(std::move(clip));
        }
    }

    if (!outModel.animations.empty()) {
        ce::diagnostics::EngineLog::Info("GLTF", "Extracted " + juce::String(static_cast<int>(outModel.animations.size())) +
                                                     " animation clip(s).");
    }
}

// Converts a glTF quaternion (x, y, z, w) into Euler angles (radians,
// applied X then Y then Z) for LoadedNode::localEulerRotationRadians --
// engine::Transform has no quaternion field, unlike LoadedJoint::bindRotation
// which stays quaternion for animation blending. Standard XYZ-order
// quaternion-to-Euler extraction; verified against identity (0,0,0,1 ->
// 0,0,0) and simple 90-degree-per-axis cases.
juce::Vector3D<float> QuaternionToEuler(float x, float y, float z, float w) {
    const float sinRollCosPitch = 2.0f * (w * x + y * z);
    const float cosRollCosPitch = 1.0f - 2.0f * (x * x + y * y);
    const float roll = std::atan2(sinRollCosPitch, cosRollCosPitch);

    const float sinPitch = 2.0f * (w * y - z * x);
    const float pitch = std::abs(sinPitch) >= 1.0f ? std::copysign(juce::MathConstants<float>::halfPi, sinPitch)
                                                     : std::asin(sinPitch);

    const float sinYawCosPitch = 2.0f * (w * z + x * y);
    const float cosYawCosPitch = 1.0f - 2.0f * (y * y + z * z);
    const float yaw = std::atan2(sinYawCosPitch, cosYawCosPitch);

    return { roll, pitch, yaw };
}

// Flattens every node in the file's scene graph into LoadedModel::nodes --
// the whole-file equivalent of ExtractSkin, but with a real parentIndex for
// every node (no "external parent = root" limitation, since every node in
// data.nodes is available to map against, not just a skin's joint list).
void ExtractNodes(const cgltf_data& data, LoadedModel& outModel) {
    std::unordered_map<const cgltf_node*, int> nodeIndexByNode;
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        nodeIndexByNode[&data.nodes[n]] = static_cast<int>(n);
    }

    outModel.nodes.resize(data.nodes_count);
    outModel.modelSpaceBasis = juce::Matrix3D<float>();
    for (cgltf_size n = 0; n < data.nodes_count; ++n) {
        const cgltf_node& node = data.nodes[n];
        LoadedNode loaded;
        loaded.name = node.name != nullptr ? juce::String(node.name) : ("Node" + juce::String(static_cast<int>(n)));

        if (node.parent != nullptr) {
            const auto it = nodeIndexByNode.find(node.parent);
            if (it != nodeIndexByNode.end()) {
                loaded.parentIndex = it->second;
            }
        }

        const bool isLegacyConversionRoot = IsLegacyBlenderConversionRoot(data, node);
        const auto nodeTransform = isLegacyConversionRoot ? NodeTransform{}
                                                          : NodeTransformIgnoringScale(node);
        loaded.localTranslation = nodeTransform.translation;
        loaded.localEulerRotationRadians = QuaternionToEuler(nodeTransform.rotation[0], nodeTransform.rotation[1],
                                                              nodeTransform.rotation[2], nodeTransform.rotation[3]);
        loaded.localScale = { 1.0f, 1.0f, 1.0f };

        if (node.mesh != nullptr) {
            loaded.meshIndex = static_cast<int>(node.mesh - data.meshes);
        }

        if (isLegacyConversionRoot) {
            // The retained vertex and skin data are Blender Z-up metres. The
            // engine is Y-up; rotate only the final rendered model, after the
            // skin palette, rather than altering inverse binds or individual
            // character placement code.
            outModel.modelSpaceBasis = juce::Matrix3D<float>::rotation(
                { -juce::MathConstants<float>::halfPi, 0.0f, 0.0f });
            ce::diagnostics::EngineLog::Info("GLTF", "Normalized legacy Blender coordinate wrapper on " + loaded.name + ".");
        }

        outModel.nodes[n] = std::move(loaded);
    }
}

// One material's texture URIs, left for the caller to resolve into a disk
// or virtual path (LoadGltf/LoadGltfFromVfs) -- mirrors LoadedMaterial's
// own three texture slots exactly, just as bare strings before either
// resolution mode applies its own base-path join.
struct MaterialTextureUris {
    juce::String baseColor;
    juce::String metallicRoughness;
    juce::String normal;
};

// Extracts materials (with each texture URI left for the caller to
// resolve into a disk or virtual path) and triangle-list primitives from
// an already-parsed+buffer-loaded cgltf_data. Shared by both LoadGltf
// and LoadGltfFromVfs — everything past "how were the bytes read" is
// identical between the two modes.
// Djehuti Bridge: pulls just "djehuti_asset_id" out of asset.extras's raw
// JSON text (cgltf_extras::data -- a plain null-terminated JSON string cgltf
// already gives us, not offset-based substring extraction). Deliberately
// narrow: no general extras system, just this one field.
juce::String ExtractDjehutiAssetId(const cgltf_asset& asset) {
    if (asset.extras.data == nullptr) return {};
    const auto parsed = juce::JSON::parse(juce::String(asset.extras.data));
    if (const auto* object = parsed.getDynamicObject())
        return object->getProperty("djehuti_asset_id").toString();
    return {};
}

// Reads one cgltf_texture's URI or copies an embedded buffer-view image
// into the imported material. Keeping the bytes avoids a temporary-file
// extraction step for self-contained GLB source art.
juce::String ExtractTextureUri(const cgltf_texture* texture, int materialIndex, const char* slotName,
                               juce::MemoryBlock& embeddedBytes, juce::String& debugName) {
    if (texture == nullptr || texture->image == nullptr) return {};
    const cgltf_image& image = *texture->image;
    const juce::String uri = image.uri != nullptr ? juce::String(image.uri) : juce::String();
    if (uri.isNotEmpty() && !uri.startsWith("data:")) return uri;

    if (image.buffer_view != nullptr && image.buffer_view->buffer != nullptr && image.buffer_view->buffer->data != nullptr) {
        const cgltf_buffer_view& view = *image.buffer_view;
        const cgltf_buffer& buffer = *view.buffer;
        if (view.offset <= buffer.size && view.size <= buffer.size - view.offset) {
            const auto* bytes = static_cast<const std::uint8_t*>(buffer.data) + view.offset;
            embeddedBytes.append(bytes, view.size);
            debugName = image.name != nullptr ? juce::String(image.name)
                                              : "embedded-material-" + juce::String(materialIndex) + "-" + slotName;
            return {};
        }
    }

    ce::diagnostics::EngineLog::Warning(
        "GLTF", "Material " + juce::String(materialIndex) + " has an embedded/data-URI " + juce::String(slotName) +
                     " image; not yet supported, skipping texture.");
    return {};
}

void ExtractModel(const cgltf_data& data, LoadedModel& outModel, std::vector<MaterialTextureUris>& outMaterialTextureUris) {
    outModel.djehutiAssetId = ExtractDjehutiAssetId(data.asset);
    outModel.materials.reserve(data.materials_count);
    outMaterialTextureUris.reserve(data.materials_count);

    for (cgltf_size m = 0; m < data.materials_count; ++m) {
        const cgltf_material& src = data.materials[m];
        LoadedMaterial material;
        MaterialTextureUris textureUris;

        if (src.has_pbr_metallic_roughness) {
            const auto& pbr = src.pbr_metallic_roughness;
            material.baseColorFactor = { pbr.base_color_factor[0], pbr.base_color_factor[1],
                                          pbr.base_color_factor[2] };
            material.metallicFactor = pbr.metallic_factor;
            material.roughnessFactor = pbr.roughness_factor;

            textureUris.baseColor = ExtractTextureUri(pbr.base_color_texture.texture, static_cast<int>(m), "base color",
                                                       material.baseColorTextureBytes, material.baseColorTextureDebugName);
            textureUris.metallicRoughness =
                ExtractTextureUri(pbr.metallic_roughness_texture.texture, static_cast<int>(m), "metallic-roughness",
                                  material.metallicRoughnessTextureBytes, material.metallicRoughnessTextureDebugName);
        }
        textureUris.normal = ExtractTextureUri(src.normal_texture.texture, static_cast<int>(m), "normal",
                                                material.normalTextureBytes, material.normalTextureDebugName);

        outModel.materials.push_back(material);
        outMaterialTextureUris.push_back(textureUris);
    }

    outModel.meshPrimitiveRanges.resize(data.meshes_count);
    for (cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex) {
        const cgltf_mesh& mesh = data.meshes[meshIndex];
        const std::size_t rangeStart = outModel.primitives.size();

        for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
            const cgltf_primitive& primitive = mesh.primitives[primIndex];
            if (primitive.type != cgltf_primitive_type_triangles) {
                ce::diagnostics::EngineLog::Warning(
                    "GLTF", "Skipping non-triangle primitive in mesh " + juce::String(static_cast<int>(meshIndex)) + ".");
                continue;
            }

            const cgltf_accessor* positionAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_position);
            if (positionAccessor == nullptr) {
                ce::diagnostics::EngineLog::Warning(
                    "GLTF", "Primitive in mesh " + juce::String(static_cast<int>(meshIndex)) + " has no POSITION attribute, skipping.");
                continue;
            }
            const cgltf_accessor* normalAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* uvAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* jointsAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_joints);
            const cgltf_accessor* weightsAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_weights);

            LoadedPrimitive loaded;
            loaded.vertices.resize(positionAccessor->count);

            for (cgltf_size v = 0; v < positionAccessor->count; ++v) {
                Vertex& vertex = loaded.vertices[v];

                float pos[3] = { 0.0f, 0.0f, 0.0f };
                cgltf_accessor_read_float(positionAccessor, v, pos, 3);
                vertex.position[0] = pos[0];
                vertex.position[1] = pos[1];
                vertex.position[2] = pos[2];

                if (normalAccessor != nullptr) {
                    float normal[3] = { 0.0f, 0.0f, 1.0f };
                    cgltf_accessor_read_float(normalAccessor, v, normal, 3);
                    vertex.normal[0] = normal[0];
                    vertex.normal[1] = normal[1];
                    vertex.normal[2] = normal[2];
                }

                if (uvAccessor != nullptr) {
                    float uv[2] = { 0.0f, 0.0f };
                    cgltf_accessor_read_float(uvAccessor, v, uv, 2);
                    vertex.uv[0] = uv[0];
                    vertex.uv[1] = uv[1];
                }

                // JOINTS_0 is always an unsigned byte/short index per the
                // glTF spec (never float, never normalized) -- read_uint
                // is the semantically correct accessor for it, unlike
                // read_float's implicit int->float conversion elsewhere
                // in this function. WEIGHTS_0 genuinely is a float (or a
                // normalized ubyte/ushort spec allows too, which
                // read_float also converts correctly).
                if (jointsAccessor != nullptr) {
                    cgltf_uint joints[4] = { 0, 0, 0, 0 };
                    cgltf_accessor_read_uint(jointsAccessor, v, joints, 4);
                    vertex.boneIndices[0] = static_cast<float>(joints[0]);
                    vertex.boneIndices[1] = static_cast<float>(joints[1]);
                    vertex.boneIndices[2] = static_cast<float>(joints[2]);
                    vertex.boneIndices[3] = static_cast<float>(joints[3]);
                }

                if (weightsAccessor != nullptr) {
                    float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    cgltf_accessor_read_float(weightsAccessor, v, weights, 4);
                    vertex.boneWeights[0] = weights[0];
                    vertex.boneWeights[1] = weights[1];
                    vertex.boneWeights[2] = weights[2];
                    vertex.boneWeights[3] = weights[3];
                }
            }

            if (primitive.indices != nullptr) {
                loaded.indices.resize(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                    loaded.indices[i] = static_cast<GLuint>(cgltf_accessor_read_index(primitive.indices, i));
                }
            } else {
                loaded.indices.resize(positionAccessor->count);
                for (cgltf_size i = 0; i < positionAccessor->count; ++i) {
                    loaded.indices[i] = static_cast<GLuint>(i);
                }
            }

            if (primitive.material != nullptr) {
                loaded.materialIndex = static_cast<int>(primitive.material - data.materials);
            }

            outModel.primitives.push_back(std::move(loaded));
        }
        outModel.meshPrimitiveRanges[meshIndex] = { rangeStart, outModel.primitives.size() - rangeStart };
    }

    // Skins are attached to nodes, not meshes/primitives -- find the
    // first node that actually uses one and extract just that skin.
    // Multiple distinct skeletons in one file is a real glTF capability
    // but not one anything in this engine needs yet.
    const cgltf_skin* foundSkin = nullptr;
    for (cgltf_size nodeIndex = 0; nodeIndex < data.nodes_count; ++nodeIndex) {
        const cgltf_node& node = data.nodes[nodeIndex];
        if (node.skin != nullptr) {
            foundSkin = node.skin;
            outModel.skin = ExtractSkin(*node.skin);
            ce::diagnostics::EngineLog::Info(
                "GLTF", "Extracted skin: " + juce::String(static_cast<int>(outModel.skin->joints.size())) + " joint(s).");
            break;
        }
    }

    ExtractAnimations(data, foundSkin, outModel);
    ExtractNodes(data, outModel);
}

cgltf_result VfsFileRead(const cgltf_memory_options* memoryOptions, const cgltf_file_options* fileOptions,
                          const char* path, cgltf_size* size, void** data) {
    auto* vfs = static_cast<creation::assets::VirtualFileSystem*>(fileOptions->user_data);

    juce::MemoryBlock block;
    if (vfs == nullptr || !vfs->readFile(juce::String(path), block)) {
        return cgltf_result_file_not_found;
    }

    void* (*memoryAlloc)(void*, cgltf_size) =
        memoryOptions->alloc_func ? memoryOptions->alloc_func : &cgltf_default_alloc;
    void* buffer = memoryAlloc(memoryOptions->user_data, block.getSize());
    if (buffer == nullptr) {
        return cgltf_result_out_of_memory;
    }
    std::memcpy(buffer, block.getData(), block.getSize());

    if (size != nullptr) {
        *size = block.getSize();
    }
    if (data != nullptr) {
        *data = buffer;
    }
    return cgltf_result_success;
}

void VfsFileRelease(const cgltf_memory_options* memoryOptions, const cgltf_file_options*, void* data, cgltf_size) {
    void (*memoryFree)(void*, void*) = memoryOptions->free_func ? memoryOptions->free_func : &cgltf_default_free;
    memoryFree(memoryOptions->user_data, data);
}

} // namespace

namespace {
// Node count is logged alongside primitive/material count on every load --
// this is exactly the number GltfAssetImporter's multi-part detection acts
// on (see docs/OBJECT_MODEL.md's "Multi-part import decomposes into
// components"), so a load's own summary line is the fastest way to see
// whether a file was even recognized as multi-part before chasing further.
int CountMeshBearingNodes(const LoadedModel& model) {
    int count = 0;
    for (const auto& node : model.nodes) {
        if (node.meshIndex >= 0) ++count;
    }
    return count;
}
} // namespace

bool LoadGltf(const juce::File& gltfFile, LoadedModel& outModel) {
    if (!gltfFile.existsAsFile()) {
        ce::diagnostics::EngineLog::Error("GLTF", "File not found: " + gltfFile.getFullPathName());
        return false;
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    const auto pathUtf8 = gltfFile.getFullPathName().toRawUTF8();

    cgltf_result result = cgltf_parse_file(&options, pathUtf8, &data);
    if (result != cgltf_result_success) {
        ce::diagnostics::EngineLog::Error("GLTF", "Parse failed (" + juce::String(static_cast<int>(result)) + "): " +
                                                       gltfFile.getFullPathName());
        return false;
    }

    result = cgltf_load_buffers(&options, data, pathUtf8);
    if (result != cgltf_result_success) {
        ce::diagnostics::EngineLog::Error("GLTF", "Failed to load buffers (" + juce::String(static_cast<int>(result)) +
                                                       ") for " + gltfFile.getFullPathName());
        cgltf_free(data);
        return false;
    }

    std::vector<MaterialTextureUris> textureUris;
    ExtractModel(*data, outModel, textureUris);

    const juce::File baseDir = gltfFile.getParentDirectory();
    for (std::size_t i = 0; i < outModel.materials.size(); ++i) {
        if (textureUris[i].baseColor.isNotEmpty()) {
            outModel.materials[i].baseColorTexturePath = baseDir.getChildFile(textureUris[i].baseColor);
        }
        if (textureUris[i].metallicRoughness.isNotEmpty()) {
            outModel.materials[i].metallicRoughnessTexturePath = baseDir.getChildFile(textureUris[i].metallicRoughness);
        }
        if (textureUris[i].normal.isNotEmpty()) {
            outModel.materials[i].normalTexturePath = baseDir.getChildFile(textureUris[i].normal);
        }
    }

    ce::diagnostics::EngineLog::Info("GLTF", "Loaded " + gltfFile.getFileName() + ": " +
                                                  juce::String(static_cast<int>(outModel.primitives.size())) +
                                                  " primitive(s), " +
                                                  juce::String(static_cast<int>(outModel.materials.size())) +
                                                  " material(s), " + juce::String(CountMeshBearingNodes(outModel)) +
                                                  " mesh-bearing node(s) of " +
                                                  juce::String(static_cast<int>(outModel.nodes.size())) + " total.");

    cgltf_free(data);
    return true;
}

bool LoadGltfFromVfs(creation::assets::VirtualFileSystem& vfs, const juce::String& virtualGltfPath, LoadedModel& outModel) {
    juce::MemoryBlock gltfBytes;
    if (!vfs.readFile(virtualGltfPath, gltfBytes)) {
        ce::diagnostics::EngineLog::Error("GLTF", "VFS entry not found: " + virtualGltfPath);
        return false;
    }

    cgltf_options options{};
    options.file.read = &VfsFileRead;
    options.file.release = &VfsFileRelease;
    options.file.user_data = &vfs;

    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, gltfBytes.getData(), gltfBytes.getSize(), &data);
    if (result != cgltf_result_success) {
        ce::diagnostics::EngineLog::Error("GLTF", "VFS parse failed (" + juce::String(static_cast<int>(result)) + "): " +
                                                       virtualGltfPath);
        return false;
    }

    // cgltf combines this path's directory with each buffer's relative
    // URI internally (cgltf_combine_paths) before calling VfsFileRead —
    // same contract as cgltf_parse_file's gltf_path argument in disk mode.
    result = cgltf_load_buffers(&options, data, virtualGltfPath.toRawUTF8());
    if (result != cgltf_result_success) {
        ce::diagnostics::EngineLog::Error("GLTF", "VFS failed to load buffers (" + juce::String(static_cast<int>(result)) +
                                                       ") for " + virtualGltfPath);
        cgltf_free(data);
        return false;
    }

    std::vector<MaterialTextureUris> textureUris;
    ExtractModel(*data, outModel, textureUris);

    // upToLastOccurrenceOf returns the whole input unchanged when the
    // substring isn't found, not empty — wrong for a flat entry name
    // with no '/' at all, so that case is handled explicitly here.
    const auto lastSlash = virtualGltfPath.lastIndexOfChar('/');
    const juce::String virtualBaseDir =
        lastSlash >= 0 ? virtualGltfPath.substring(0, lastSlash + 1) : juce::String();
    for (std::size_t i = 0; i < outModel.materials.size(); ++i) {
        if (textureUris[i].baseColor.isNotEmpty()) {
            outModel.materials[i].baseColorTextureVirtualPath = virtualBaseDir + textureUris[i].baseColor;
        }
        if (textureUris[i].metallicRoughness.isNotEmpty()) {
            outModel.materials[i].metallicRoughnessTextureVirtualPath = virtualBaseDir + textureUris[i].metallicRoughness;
        }
        if (textureUris[i].normal.isNotEmpty()) {
            outModel.materials[i].normalTextureVirtualPath = virtualBaseDir + textureUris[i].normal;
        }
    }

    ce::diagnostics::EngineLog::Info("GLTF", "VFS loaded " + virtualGltfPath + ": " +
                                                  juce::String(static_cast<int>(outModel.primitives.size())) +
                                                  " primitive(s), " +
                                                  juce::String(static_cast<int>(outModel.materials.size())) +
                                                  " material(s), " + juce::String(CountMeshBearingNodes(outModel)) +
                                                  " mesh-bearing node(s) of " +
                                                  juce::String(static_cast<int>(outModel.nodes.size())) + " total.");

    cgltf_free(data);
    return true;
}

} // namespace ce
