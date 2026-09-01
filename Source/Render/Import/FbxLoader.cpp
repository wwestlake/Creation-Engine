#include "Render/Import/FbxLoader.h"

#include "ufbx.h"

namespace ce {

bool LoadFbx(const juce::File& fbxFile, LoadedModel& outModel, juce::String& errorMessage)
{
    outModel = {};
    if (! fbxFile.existsAsFile()) {
        errorMessage = "The FBX source file does not exist.";
        return false;
    }

    ufbx_load_opts options = {};
    options.generate_missing_normals = true;
    options.load_external_files = true;
    options.ignore_missing_external_files = true;
    options.target_unit_meters = 1.0f;

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

    const auto* mesh = scene->meshes.data[0];
    LoadedPrimitive primitive;
    primitive.vertices.reserve(mesh->num_triangles * 3);
    primitive.indices.reserve(mesh->num_triangles * 3);
    std::vector<uint32_t> triangleIndices(mesh->max_face_triangles * 3);

    for (size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex) {
        const auto face = mesh->faces[faceIndex];
        if (face.num_indices < 3)
            continue;

        const auto triangleIndexCount = ufbx_triangulate_face(
            triangleIndices.data(), triangleIndices.size(), mesh, face);
        for (uint32_t corner = 0; corner < triangleIndexCount; ++corner) {
            const auto sourceIndex = triangleIndices[corner];
            const auto position = ufbx_get_vertex_vec3(&mesh->vertex_position, sourceIndex);
            const auto normal = mesh->vertex_normal.exists
                                  ? ufbx_get_vertex_vec3(&mesh->vertex_normal, sourceIndex)
                                  : ufbx_vec3{ 0.0, 1.0, 0.0 };
            const auto uv = mesh->vertex_uv.exists
                              ? ufbx_get_vertex_vec2(&mesh->vertex_uv, sourceIndex)
                              : ufbx_vec2{ 0.0, 0.0 };

            Vertex vertex{};
            vertex.position[0] = static_cast<float>(position.x);
            vertex.position[1] = static_cast<float>(position.y);
            vertex.position[2] = static_cast<float>(position.z);
            vertex.normal[0] = static_cast<float>(normal.x);
            vertex.normal[1] = static_cast<float>(normal.y);
            vertex.normal[2] = static_cast<float>(normal.z);
            vertex.uv[0] = static_cast<float>(uv.x);
            vertex.uv[1] = static_cast<float>(uv.y);
            primitive.indices.push_back(static_cast<GLuint>(primitive.vertices.size()));
            primitive.vertices.push_back(vertex);
        }
    }

    ufbx_free_scene(scene);
    if (primitive.vertices.empty()) {
        errorMessage = "The FBX mesh contains no renderable triangles.";
        return false;
    }

    outModel.primitives.push_back(std::move(primitive));
    outModel.materials.emplace_back();
    outModel.primitives.front().materialIndex = 0;
    return true;
}

} // namespace ce
