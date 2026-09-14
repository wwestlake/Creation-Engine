#include "Render/Import/FbxLoader.h"

#include <cmath>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ufbx.h"

#include "Diagnostics/EngineLog.h"

namespace ce {

namespace {

// ufbx_matrix is a 3x4 affine matrix (3 orthonormal-ish column vectors plus
// translation, row-major within each ufbx_vec3 column). juce::Matrix3D<float>
// is a plain 4x4 column-major float array -- pad the missing bottom row with
// the usual (0,0,0,1).
juce::Matrix3D<float> ToJuceMatrix(const ufbx_matrix& m) {
    const float values[16] = {
        (float) m.cols[0].x, (float) m.cols[0].y, (float) m.cols[0].z, 0.0f,
        (float) m.cols[1].x, (float) m.cols[1].y, (float) m.cols[1].z, 0.0f,
        (float) m.cols[2].x, (float) m.cols[2].y, (float) m.cols[2].z, 0.0f,
        (float) m.cols[3].x, (float) m.cols[3].y, (float) m.cols[3].z, 1.0f,
    };
    return juce::Matrix3D<float>(values);
}

juce::Vector3D<float> ToJuceVec3(const ufbx_vec3& v) {
    return { (float) v.x, (float) v.y, (float) v.z };
}

juce::String ToJuceString(const ufbx_string& s) {
    return juce::String(juce::CharPointer_UTF8(s.data), (int) s.length);
}

// Same XYZ-order quaternion-to-Euler extraction GltfLoader.cpp uses for
// LoadedNode::localEulerRotationRadians -- duplicated rather than shared
// because it's a small pure function and neither loader depends on the
// other today.
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

struct ExtractedSkin {
    LoadedSkin skin;
    std::unordered_map<const ufbx_node*, int> jointIndexByNode;
    // Per skin_deformer, maps that deformer's own local cluster index
    // (ufbx_skin_weight::cluster_index) to the flattened joint index above --
    // needed because two different meshes' deformers number their clusters
    // independently even when they share the same underlying bones.
    std::unordered_map<const ufbx_skin_deformer*, std::vector<int>> clusterToJointByDeformer;
};

// Flattens every skin deformer's clusters (bones) in the scene into one
// joint list, the FBX equivalent of GltfLoader's ExtractSkin -- except a
// glTF file has one skin per skinned node while an FBX scene can have one
// skin_deformer per skinned mesh, potentially sharing bones. A bone
// (ufbx_node) seen from more than one deformer becomes a single joint.
std::optional<ExtractedSkin> ExtractSkin(const ufbx_scene& scene) {
    if (scene.skin_deformers.count == 0) {
        return std::nullopt;
    }

    ExtractedSkin result;

    // Pass 1: register every distinct bone node as a joint (name only --
    // parent linkage needs every joint's index to already be known).
    for (size_t d = 0; d < scene.skin_deformers.count; ++d) {
        const ufbx_skin_deformer* deformer = scene.skin_deformers.data[d];
        auto& clusterMap = result.clusterToJointByDeformer[deformer];
        clusterMap.resize(deformer->clusters.count, -1);

        for (size_t c = 0; c < deformer->clusters.count; ++c) {
            const ufbx_skin_cluster* cluster = deformer->clusters.data[c];
            if (cluster->bone_node == nullptr) {
                continue;
            }

            auto [it, inserted] = result.jointIndexByNode.try_emplace(
                cluster->bone_node, static_cast<int>(result.skin.joints.size()));
            if (inserted) {
                LoadedJoint joint;
                joint.name = ToJuceString(cluster->bone_node->name);
                result.skin.joints.push_back(joint);
            }
            clusterMap[c] = it->second;
        }
    }

    if (result.skin.joints.empty()) {
        return std::nullopt;
    }

    // Pass 2: parent linkage, bind transform, and inverse bind matrix, now
    // that every bone node's joint index is resolvable.
    for (size_t d = 0; d < scene.skin_deformers.count; ++d) {
        const ufbx_skin_deformer* deformer = scene.skin_deformers.data[d];
        for (size_t c = 0; c < deformer->clusters.count; ++c) {
            const ufbx_skin_cluster* cluster = deformer->clusters.data[c];
            const ufbx_node* node = cluster->bone_node;
            if (node == nullptr) {
                continue;
            }
            const int jointIndex = result.jointIndexByNode.at(node);
            LoadedJoint& joint = result.skin.joints[jointIndex];

            if (node->parent != nullptr) {
                const auto parentIt = result.jointIndexByNode.find(node->parent);
                if (parentIt != result.jointIndexByNode.end()) {
                    joint.parentIndex = parentIt->second;
                }
            }

            joint.localBindTransform = ToJuceMatrix(node->node_to_parent);
            joint.bindTranslation = ToJuceVec3(node->local_transform.translation);
            joint.bindRotation[0] = (float) node->local_transform.rotation.x;
            joint.bindRotation[1] = (float) node->local_transform.rotation.y;
            joint.bindRotation[2] = (float) node->local_transform.rotation.z;
            joint.bindRotation[3] = (float) node->local_transform.rotation.w;
            joint.bindScale = ToJuceVec3(node->local_transform.scale);
            // geometry_to_bone is exactly glTF's inverse bind matrix: mesh-space
            // vertex positions -> this bone's space, at bind pose.
            joint.inverseBindMatrix = ToJuceMatrix(cluster->geometry_to_bone);

            if (joint.parentIndex < 0) {
                juce::Matrix3D<float> ancestor;
                std::vector<const ufbx_node*> chain;
                for (const ufbx_node* p = node->parent; p != nullptr; p = p->parent) {
                    if (result.jointIndexByNode.find(p) != result.jointIndexByNode.end()) {
                        break;
                    }
                    chain.push_back(p);
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    ancestor = ancestor * ToJuceMatrix((*it)->node_to_parent);
                }
                joint.rootParentBindTransform = ancestor;
            }
        }
    }

    ce::diagnostics::EngineLog::Info(
        "FBX", "Extracted skin: " + juce::String(static_cast<int>(result.skin.joints.size())) + " joint(s).");
    return result;
}

// Bakes every animation stack (FBX "take") into an AnimationClip, keeping
// only channels that target a joint in `skin` -- root motion, cameras, and
// non-skinned nodes are out of scope, mirroring GltfLoader::ExtractAnimations.
// Baking (rather than reading raw curves, as the glTF path does with glTF's
// own sparse sampler keys) is the simplest robust way to get resampled,
// already-evaluated keyframes across FBX's much more varied
// layer/curve/property animation model.
void ExtractAnimations(const ufbx_scene& scene, const ExtractedSkin* skin, LoadedModel& outModel) {
    if (skin == nullptr || scene.anim_stacks.count == 0) {
        return;
    }

    for (size_t s = 0; s < scene.anim_stacks.count; ++s) {
        const ufbx_anim_stack* stack = scene.anim_stacks.data[s];

        ufbx_bake_opts bakeOpts = {};
        ufbx_error bakeError;
        ufbx_baked_anim* baked = ufbx_bake_anim(&scene, stack->anim, &bakeOpts, &bakeError);
        if (baked == nullptr) {
            ce::diagnostics::EngineLog::Warning(
                "FBX", "Failed to bake animation stack " + ToJuceString(stack->name) + ": "
                           + juce::String(bakeError.description.data, (int) bakeError.description.length));
            continue;
        }

        AnimationClip clip;
        clip.name = stack->name.length > 0 ? ToJuceString(stack->name) : ("Animation" + juce::String(static_cast<int>(s)));

        for (size_t n = 0; n < baked->nodes.count; ++n) {
            const ufbx_baked_node& bakedNode = baked->nodes.data[n];
            if (bakedNode.typed_id >= scene.nodes.count) {
                continue;
            }
            const ufbx_node* node = scene.nodes.data[bakedNode.typed_id];
            const auto jointIt = skin->jointIndexByNode.find(node);
            if (jointIt == skin->jointIndexByNode.end()) {
                continue; // targets a node outside the skin -- not this pass's concern.
            }

            if (bakedNode.translation_keys.count > 0) {
                AnimationChannel channel;
                channel.jointIndex = jointIt->second;
                channel.path = AnimationChannel::Path::Translation;
                channel.interpolation = AnimationInterpolation::Linear;
                channel.times.reserve(bakedNode.translation_keys.count);
                channel.values.reserve(bakedNode.translation_keys.count * 3);
                for (size_t k = 0; k < bakedNode.translation_keys.count; ++k) {
                    const auto& key = bakedNode.translation_keys.data[k];
                    channel.times.push_back((float) key.time);
                    channel.values.push_back((float) key.value.x);
                    channel.values.push_back((float) key.value.y);
                    channel.values.push_back((float) key.value.z);
                }
                clip.duration = juce::jmax(clip.duration, channel.times.back());
                clip.channels.push_back(std::move(channel));
            }

            if (bakedNode.rotation_keys.count > 0) {
                AnimationChannel channel;
                channel.jointIndex = jointIt->second;
                channel.path = AnimationChannel::Path::Rotation;
                channel.interpolation = AnimationInterpolation::Linear;
                channel.times.reserve(bakedNode.rotation_keys.count);
                channel.values.reserve(bakedNode.rotation_keys.count * 4);
                for (size_t k = 0; k < bakedNode.rotation_keys.count; ++k) {
                    const auto& key = bakedNode.rotation_keys.data[k];
                    channel.times.push_back((float) key.time);
                    channel.values.push_back((float) key.value.x);
                    channel.values.push_back((float) key.value.y);
                    channel.values.push_back((float) key.value.z);
                    channel.values.push_back((float) key.value.w);
                }
                clip.duration = juce::jmax(clip.duration, channel.times.back());
                clip.channels.push_back(std::move(channel));
            }

            if (bakedNode.scale_keys.count > 0) {
                AnimationChannel channel;
                channel.jointIndex = jointIt->second;
                channel.path = AnimationChannel::Path::Scale;
                channel.interpolation = AnimationInterpolation::Linear;
                channel.times.reserve(bakedNode.scale_keys.count);
                channel.values.reserve(bakedNode.scale_keys.count * 3);
                for (size_t k = 0; k < bakedNode.scale_keys.count; ++k) {
                    const auto& key = bakedNode.scale_keys.data[k];
                    channel.times.push_back((float) key.time);
                    channel.values.push_back((float) key.value.x);
                    channel.values.push_back((float) key.value.y);
                    channel.values.push_back((float) key.value.z);
                }
                clip.duration = juce::jmax(clip.duration, channel.times.back());
                clip.channels.push_back(std::move(channel));
            }
        }

        ufbx_free_baked_anim(baked);

        if (!clip.channels.empty()) {
            outModel.animations.push_back(std::move(clip));
        }
    }

    if (!outModel.animations.empty()) {
        ce::diagnostics::EngineLog::Info(
            "FBX", "Extracted " + juce::String(static_cast<int>(outModel.animations.size())) + " animation clip(s).");
    }
}

// Every node in the scene (matching GltfLoader::ExtractNodes), including
// the implicit root -- LoadedNode::parentIndex is -1 for it same as any
// true scene root.
void ExtractNodes(const ufbx_scene& scene, const std::unordered_map<const ufbx_mesh*, int>& meshIndexByPtr,
                   LoadedModel& outModel) {
    std::unordered_map<const ufbx_node*, int> nodeIndexByNode;
    for (size_t n = 0; n < scene.nodes.count; ++n) {
        nodeIndexByNode[scene.nodes.data[n]] = static_cast<int>(n);
    }

    outModel.nodes.resize(scene.nodes.count);
    for (size_t n = 0; n < scene.nodes.count; ++n) {
        const ufbx_node* node = scene.nodes.data[n];
        LoadedNode loaded;
        loaded.name = ToJuceString(node->name);

        if (node->parent != nullptr) {
            const auto it = nodeIndexByNode.find(node->parent);
            if (it != nodeIndexByNode.end()) {
                loaded.parentIndex = it->second;
            }
        }

        const auto& t = node->local_transform;
        loaded.localTranslation = ToJuceVec3(t.translation);
        loaded.localEulerRotationRadians = QuaternionToEuler((float) t.rotation.x, (float) t.rotation.y,
                                                               (float) t.rotation.z, (float) t.rotation.w);
        loaded.localScale = ToJuceVec3(t.scale);

        if (node->mesh != nullptr) {
            const auto it = meshIndexByPtr.find(node->mesh);
            if (it != meshIndexByPtr.end()) {
                loaded.meshIndex = it->second;
            }
        }

        outModel.nodes[n] = std::move(loaded);
    }
}

} // namespace

bool LoadFbx(const juce::File& fbxFile, LoadedModel& outModel, juce::String& errorMessage) {
    outModel = {};
    if (! fbxFile.existsAsFile()) {
        errorMessage = "The FBX source file does not exist.";
        return false;
    }

    ufbx_load_opts options = {};
    options.generate_missing_normals = true;
    options.load_external_files = true;
    options.ignore_missing_external_files = true;
    // Bake axis/unit conversion straight into every vertex and node
    // transform at load time -- Djehuti's canonical space (one metre per
    // unit, right-handed Y-up: MODEL_IMPORT_CONTRACT.md) is exactly
    // ufbx_axes_right_handed_y_up, so unlike the glTF path there is no
    // per-source-tool "legacy wrapper" heuristic needed here.
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0f;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

    ufbx_error error = {};
    const auto filename = fbxFile.getFullPathName().toRawUTF8();
    auto* scene = ufbx_load_file(filename, &options, &error);
    if (scene == nullptr) {
        errorMessage = "ufbx could not load the FBX scene: "
                      + juce::String(error.description.data, (int) error.description.length);
        return false;
    }

    if (scene->meshes.count == 0) {
        errorMessage = "The FBX contains no polygon meshes.";
        ufbx_free_scene(scene);
        return false;
    }

    // Materials, flattened once up front so every mesh part below can
    // resolve its material by pointer regardless of per-mesh local ordering.
    std::unordered_map<const ufbx_material*, int> materialIndexByPtr;
    outModel.materials.reserve(scene->materials.count);
    for (size_t m = 0; m < scene->materials.count; ++m) {
        const ufbx_material* material = scene->materials.data[m];
        materialIndexByPtr[material] = static_cast<int>(outModel.materials.size());

        LoadedMaterial loaded;
        const auto& pbr = material->pbr;
        if (pbr.base_color.has_value) {
            loaded.baseColorFactor = ToJuceVec3(pbr.base_color.value_vec3);
        }
        if (pbr.roughness.has_value) {
            loaded.roughnessFactor = (float) pbr.roughness.value_real;
        }
        if (pbr.metalness.has_value) {
            loaded.metallicFactor = (float) pbr.metalness.value_real;
        }

        const auto resolveTexture = [&fbxFile](const ufbx_material_map& map, juce::File& outPath,
                                                juce::MemoryBlock& outBytes, juce::String& outDebugName) {
            if (map.texture == nullptr) {
                return;
            }
            const ufbx_texture& texture = *map.texture;
            if (texture.content.size > 0) {
                outBytes.append(texture.content.data, texture.content.size);
                outDebugName = ToJuceString(texture.name);
                return;
            }
            if (texture.absolute_filename.length > 0) {
                outPath = juce::File(ToJuceString(texture.absolute_filename));
                return;
            }
            if (texture.relative_filename.length > 0) {
                outPath = fbxFile.getParentDirectory().getChildFile(ToJuceString(texture.relative_filename));
            }
        };

        resolveTexture(pbr.base_color, loaded.baseColorTexturePath, loaded.baseColorTextureBytes,
                        loaded.baseColorTextureDebugName);
        // glTF packs metallic+roughness into one texture (G=roughness, B=metallic);
        // FBX/ufbx keeps them as separate maps. Prefer roughness's texture for that
        // combined slot since roughness reads more often than metalness in practice
        // for these MakeHuman-sourced materials, which are not physically metallic.
        resolveTexture(pbr.roughness.texture != nullptr ? pbr.roughness : pbr.metalness,
                        loaded.metallicRoughnessTexturePath, loaded.metallicRoughnessTextureBytes,
                        loaded.metallicRoughnessTextureDebugName);
        resolveTexture(pbr.normal_map, loaded.normalTexturePath, loaded.normalTextureBytes,
                        loaded.normalTextureDebugName);

        outModel.materials.push_back(std::move(loaded));
    }

    auto skinResult = ExtractSkin(*scene);

    std::unordered_map<const ufbx_mesh*, int> meshIndexByPtr;
    outModel.meshPrimitiveRanges.resize(scene->meshes.count);
    std::vector<uint32_t> triangleIndices;

    for (size_t meshIndex = 0; meshIndex < scene->meshes.count; ++meshIndex) {
        const ufbx_mesh* mesh = scene->meshes.data[meshIndex];
        meshIndexByPtr[mesh] = static_cast<int>(meshIndex);
        const std::size_t rangeStart = outModel.primitives.size();

        const ufbx_skin_deformer* deformer = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
        const std::vector<int>* clusterToJoint = nullptr;
        if (deformer != nullptr && skinResult.has_value()) {
            const auto it = skinResult->clusterToJointByDeformer.find(deformer);
            if (it != skinResult->clusterToJointByDeformer.end()) {
                clusterToJoint = &it->second;
            }
        }

        triangleIndices.resize(mesh->max_face_triangles * 3);

        for (size_t partIndex = 0; partIndex < mesh->material_parts.count; ++partIndex) {
            const ufbx_mesh_part& part = mesh->material_parts.data[partIndex];
            if (part.num_faces == 0) {
                continue;
            }

            LoadedPrimitive primitive;
            primitive.vertices.reserve(part.num_triangles * 3);
            primitive.indices.reserve(part.num_triangles * 3);

            for (size_t f = 0; f < part.face_indices.count; ++f) {
                const ufbx_face face = mesh->faces.data[part.face_indices.data[f]];
                if (face.num_indices < 3) {
                    continue;
                }

                const uint32_t triangleCount =
                    ufbx_triangulate_face(triangleIndices.data(), triangleIndices.size(), mesh, face);
                for (uint32_t corner = 0; corner < triangleCount; ++corner) {
                    const uint32_t sourceIndex = triangleIndices[corner];
                    const ufbx_vec3 position = ufbx_get_vertex_vec3(&mesh->vertex_position, sourceIndex);
                    const ufbx_vec3 normal = mesh->vertex_normal.exists
                                                  ? ufbx_get_vertex_vec3(&mesh->vertex_normal, sourceIndex)
                                                  : ufbx_vec3{ 0.0, 1.0, 0.0 };
                    const ufbx_vec2 uv =
                        mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, sourceIndex) : ufbx_vec2{ 0.0, 0.0 };

                    Vertex vertex{};
                    vertex.position[0] = (float) position.x;
                    vertex.position[1] = (float) position.y;
                    vertex.position[2] = (float) position.z;
                    vertex.normal[0] = (float) normal.x;
                    vertex.normal[1] = (float) normal.y;
                    vertex.normal[2] = (float) normal.z;
                    vertex.uv[0] = (float) uv.x;
                    vertex.uv[1] = (float) uv.y;

                    if (deformer != nullptr && clusterToJoint != nullptr) {
                        const uint32_t logicalVertex = mesh->vertex_indices.data[sourceIndex];
                        if (logicalVertex < deformer->vertices.count) {
                            const ufbx_skin_vertex& skinVertex = deformer->vertices.data[logicalVertex];
                            const uint32_t weightCount = juce::jmin<uint32_t>(4, skinVertex.num_weights);
                            float totalWeight = 0.0f;
                            for (uint32_t w = 0; w < weightCount; ++w) {
                                const ufbx_skin_weight& weight = deformer->weights.data[skinVertex.weight_begin + w];
                                const int joint = weight.cluster_index < clusterToJoint->size()
                                                       ? (*clusterToJoint)[weight.cluster_index]
                                                       : -1;
                                if (joint < 0) {
                                    continue;
                                }
                                vertex.boneIndices[w] = (float) joint;
                                vertex.boneWeights[w] = (float) weight.weight;
                                totalWeight += (float) weight.weight;
                            }
                            if (totalWeight > 0.0f) {
                                for (auto& weightComponent : vertex.boneWeights) {
                                    weightComponent /= totalWeight;
                                }
                            }
                        }
                    }

                    primitive.indices.push_back(static_cast<GLuint>(primitive.vertices.size()));
                    primitive.vertices.push_back(vertex);
                }
            }

            if (primitive.vertices.empty()) {
                continue;
            }

            if (partIndex < mesh->materials.count) {
                const auto it = materialIndexByPtr.find(mesh->materials.data[partIndex]);
                if (it != materialIndexByPtr.end()) {
                    primitive.materialIndex = it->second;
                }
            }

            outModel.primitives.push_back(std::move(primitive));
        }

        outModel.meshPrimitiveRanges[meshIndex] = { rangeStart, outModel.primitives.size() - rangeStart };
    }

    if (outModel.primitives.empty()) {
        errorMessage = "The FBX mesh contains no renderable triangles.";
        ufbx_free_scene(scene);
        return false;
    }

    if (outModel.materials.empty()) {
        outModel.materials.emplace_back();
    }

    if (skinResult.has_value()) {
        outModel.skin = std::move(skinResult->skin);
        ExtractAnimations(*scene, &*skinResult, outModel);
    }

    ExtractNodes(*scene, meshIndexByPtr, outModel);

    ufbx_free_scene(scene);
    return true;
}

} // namespace ce
