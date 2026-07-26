#include "Render/Import/GltfLoader.h"

#include <cstring>
#include <iostream>
#include <utility>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

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

// Extracts materials (with baseColorTextureUri left for the caller to
// resolve into a disk or virtual path) and triangle-list primitives from
// an already-parsed+buffer-loaded cgltf_data. Shared by both LoadGltf
// and LoadGltfFromVfs — everything past "how were the bytes read" is
// identical between the two modes.
void ExtractModel(const cgltf_data& data, LoadedModel& outModel, std::vector<juce::String>& outMaterialTextureUris) {
    outModel.materials.reserve(data.materials_count);
    outMaterialTextureUris.reserve(data.materials_count);

    for (cgltf_size m = 0; m < data.materials_count; ++m) {
        const cgltf_material& src = data.materials[m];
        LoadedMaterial material;
        juce::String textureUri;

        if (src.has_pbr_metallic_roughness) {
            const auto& pbr = src.pbr_metallic_roughness;
            material.baseColorFactor = { pbr.base_color_factor[0], pbr.base_color_factor[1],
                                          pbr.base_color_factor[2] };
            material.metallicFactor = pbr.metallic_factor;
            material.roughnessFactor = pbr.roughness_factor;

            const cgltf_texture* baseColorTexture = pbr.base_color_texture.texture;
            if (baseColorTexture != nullptr && baseColorTexture->image != nullptr) {
                const cgltf_image& image = *baseColorTexture->image;
                const juce::String uri = image.uri != nullptr ? juce::String(image.uri) : juce::String();

                if (uri.isNotEmpty() && !uri.startsWith("data:")) {
                    textureUri = uri;
                } else {
                    // Embedded (buffer_view) or base64 data-URI images aren't
                    // decoded yet — Texture2D only reads named files/entries today.
                    std::cout << "[gltf] material " << m
                               << " has an embedded/data-URI base color image; not yet supported, skipping texture."
                               << std::endl;
                }
            }
        }

        outModel.materials.push_back(material);
        outMaterialTextureUris.push_back(textureUri);
    }

    for (cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex) {
        const cgltf_mesh& mesh = data.meshes[meshIndex];

        for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
            const cgltf_primitive& primitive = mesh.primitives[primIndex];
            if (primitive.type != cgltf_primitive_type_triangles) {
                std::cout << "[gltf] skipping non-triangle primitive in mesh " << meshIndex << std::endl;
                continue;
            }

            const cgltf_accessor* positionAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_position);
            if (positionAccessor == nullptr) {
                std::cout << "[gltf] primitive in mesh " << meshIndex << " has no POSITION attribute, skipping"
                           << std::endl;
                continue;
            }
            const cgltf_accessor* normalAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* uvAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_texcoord);

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
    }
}

cgltf_result VfsFileRead(const cgltf_memory_options* memoryOptions, const cgltf_file_options* fileOptions,
                          const char* path, cgltf_size* size, void** data) {
    auto* vfs = static_cast<assets::VirtualFileSystem*>(fileOptions->user_data);

    juce::MemoryBlock block;
    if (vfs == nullptr || !vfs->ReadFile(juce::String(path), block)) {
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

bool LoadGltf(const juce::File& gltfFile, LoadedModel& outModel) {
    if (!gltfFile.existsAsFile()) {
        std::cout << "[gltf] file not found: " << gltfFile.getFullPathName() << std::endl;
        return false;
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    const auto pathUtf8 = gltfFile.getFullPathName().toRawUTF8();

    cgltf_result result = cgltf_parse_file(&options, pathUtf8, &data);
    if (result != cgltf_result_success) {
        std::cout << "[gltf] parse failed (" << static_cast<int>(result) << "): " << gltfFile.getFullPathName()
                   << std::endl;
        return false;
    }

    result = cgltf_load_buffers(&options, data, pathUtf8);
    if (result != cgltf_result_success) {
        std::cout << "[gltf] failed to load buffers (" << static_cast<int>(result) << ") for "
                   << gltfFile.getFullPathName() << std::endl;
        cgltf_free(data);
        return false;
    }

    std::vector<juce::String> textureUris;
    ExtractModel(*data, outModel, textureUris);

    const juce::File baseDir = gltfFile.getParentDirectory();
    for (std::size_t i = 0; i < outModel.materials.size(); ++i) {
        if (textureUris[i].isNotEmpty()) {
            outModel.materials[i].baseColorTexturePath = baseDir.getChildFile(textureUris[i]);
        }
    }

    std::cout << "[gltf] loaded " << gltfFile.getFileName() << ": " << outModel.primitives.size()
               << " primitive(s), " << outModel.materials.size() << " material(s)" << std::endl;

    cgltf_free(data);
    return true;
}

bool LoadGltfFromVfs(assets::VirtualFileSystem& vfs, const juce::String& virtualGltfPath, LoadedModel& outModel) {
    juce::MemoryBlock gltfBytes;
    if (!vfs.ReadFile(virtualGltfPath, gltfBytes)) {
        std::cout << "[gltf] VFS: entry not found: " << virtualGltfPath << std::endl;
        return false;
    }

    cgltf_options options{};
    options.file.read = &VfsFileRead;
    options.file.release = &VfsFileRelease;
    options.file.user_data = &vfs;

    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, gltfBytes.getData(), gltfBytes.getSize(), &data);
    if (result != cgltf_result_success) {
        std::cout << "[gltf] VFS: parse failed (" << static_cast<int>(result) << "): " << virtualGltfPath
                   << std::endl;
        return false;
    }

    // cgltf combines this path's directory with each buffer's relative
    // URI internally (cgltf_combine_paths) before calling VfsFileRead —
    // same contract as cgltf_parse_file's gltf_path argument in disk mode.
    result = cgltf_load_buffers(&options, data, virtualGltfPath.toRawUTF8());
    if (result != cgltf_result_success) {
        std::cout << "[gltf] VFS: failed to load buffers (" << static_cast<int>(result) << ") for "
                   << virtualGltfPath << std::endl;
        cgltf_free(data);
        return false;
    }

    std::vector<juce::String> textureUris;
    ExtractModel(*data, outModel, textureUris);

    // upToLastOccurrenceOf returns the whole input unchanged when the
    // substring isn't found, not empty — wrong for a flat entry name
    // with no '/' at all, so that case is handled explicitly here.
    const auto lastSlash = virtualGltfPath.lastIndexOfChar('/');
    const juce::String virtualBaseDir =
        lastSlash >= 0 ? virtualGltfPath.substring(0, lastSlash + 1) : juce::String();
    for (std::size_t i = 0; i < outModel.materials.size(); ++i) {
        if (textureUris[i].isNotEmpty()) {
            outModel.materials[i].baseColorTextureVirtualPath = virtualBaseDir + textureUris[i];
        }
    }

    std::cout << "[gltf] VFS: loaded " << virtualGltfPath << ": " << outModel.primitives.size() << " primitive(s), "
               << outModel.materials.size() << " material(s)" << std::endl;

    cgltf_free(data);
    return true;
}

} // namespace ce
