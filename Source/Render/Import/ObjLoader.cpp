#include "Render/Import/ObjLoader.h"

#include <array>
#include <iostream>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace ce {

namespace {

// tinyobjloader's PBR extension fields (roughness/metallic) default to 0
// for any .mtl that doesn't explicitly author them -- which is nearly
// every real-world .mtl file, since PBR .mtl authoring
// (http://exocortex.com/blog/extending_wavefront_mtl_to_support_pbr) is a
// niche extension, not the format's mainstream. Trusting those blindly
// would render almost every imported OBJ as a polished mirror-metal
// instead of the matte/plastic look its diffuse color implies. Until a
// real need for PBR-authored .mtl content shows up, this only takes the
// diffuse color and leaves roughness/metallic at LoadedMaterial's own
// neutral defaults.
LoadedMaterial ExtractMaterial(const tinyobj::material_t& material) {
    LoadedMaterial loaded;
    loaded.baseColorFactor = { material.diffuse[0], material.diffuse[1], material.diffuse[2] };
    return loaded;
}

juce::Vector3D<float> ComputeFaceNormal(const Vertex& a, const Vertex& b, const Vertex& c) {
    const juce::Vector3D<float> pa{ a.position[0], a.position[1], a.position[2] };
    const juce::Vector3D<float> pb{ b.position[0], b.position[1], b.position[2] };
    const juce::Vector3D<float> pc{ c.position[0], c.position[1], c.position[2] };
    const auto normal = (pb - pa) ^ (pc - pa); // juce::Vector3D's operator^ is cross product.
    const auto length = normal.length();
    return length > 1.0e-8f ? normal / length : juce::Vector3D<float>{ 0.0f, 1.0f, 0.0f };
}

} // namespace

bool LoadObj(const juce::File& objFile, LoadedModel& outModel) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const std::string path = objFile.getFullPathName().toStdString();
    const std::string mtlBaseDir = objFile.getParentDirectory().getFullPathName().toStdString();

    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), mtlBaseDir.c_str(),
                                      /*triangulate=*/true);
    if (!warn.empty()) {
        std::cout << "[obj] " << objFile.getFileName() << ": " << warn << std::endl;
    }
    if (!ok) {
        std::cout << "[obj] failed to load " << objFile.getFileName() << ": " << err << std::endl;
        return false;
    }

    outModel = LoadedModel{};
    for (const auto& material : materials) {
        outModel.materials.push_back(ExtractMaterial(material));
    }

    LoadedPrimitive primitive;
    // First face carrying a valid material_ids[] entry decides the whole
    // merged primitive's material -- matches this loader's own "merge
    // every shape into one primitive" v1 scope (see ObjLoader.h).
    int materialIndex = -1;

    for (const auto& shape : shapes) {
        std::size_t indexOffset = 0;
        for (std::size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
            const unsigned int faceVertexCount = shape.mesh.num_face_vertices[face];
            // triangulate=true above guarantees this; skipping instead of
            // asserting is cheap insurance against a future
            // triangulate=false rather than crashing on a stray polygon.
            if (faceVertexCount != 3) {
                indexOffset += faceVertexCount;
                continue;
            }

            if (materialIndex < 0 && face < shape.mesh.material_ids.size() && shape.mesh.material_ids[face] >= 0) {
                materialIndex = shape.mesh.material_ids[face];
            }

            std::array<Vertex, 3> triangle{};
            bool anyNormalAuthored = false;
            for (unsigned int v = 0; v < 3; ++v) {
                const tinyobj::index_t index = shape.mesh.indices[indexOffset + v];
                Vertex& vertex = triangle[v];

                const auto vi = static_cast<std::size_t>(index.vertex_index);
                vertex.position[0] = attrib.vertices[3 * vi + 0];
                vertex.position[1] = attrib.vertices[3 * vi + 1];
                vertex.position[2] = attrib.vertices[3 * vi + 2];

                if (index.normal_index >= 0) {
                    const auto ni = static_cast<std::size_t>(index.normal_index);
                    vertex.normal[0] = attrib.normals[3 * ni + 0];
                    vertex.normal[1] = attrib.normals[3 * ni + 1];
                    vertex.normal[2] = attrib.normals[3 * ni + 2];
                    anyNormalAuthored = true;
                }

                if (index.texcoord_index >= 0) {
                    const auto ti = static_cast<std::size_t>(index.texcoord_index);
                    vertex.uv[0] = attrib.texcoords[2 * ti + 0];
                    vertex.uv[1] = attrib.texcoords[2 * ti + 1];
                }
            }

            if (!anyNormalAuthored) {
                // No 'vn' data for this face at all -- a flat computed
                // normal beats every vertex silently rendering as unlit
                // (a (0,0,0) normal zeroes every N.L lighting term).
                const auto flat = ComputeFaceNormal(triangle[0], triangle[1], triangle[2]);
                for (auto& vertex : triangle) {
                    vertex.normal[0] = flat.x;
                    vertex.normal[1] = flat.y;
                    vertex.normal[2] = flat.z;
                }
            }

            for (const auto& vertex : triangle) {
                primitive.indices.push_back(static_cast<GLuint>(primitive.vertices.size()));
                primitive.vertices.push_back(vertex);
            }

            indexOffset += faceVertexCount;
        }
    }

    if (primitive.vertices.empty()) {
        std::cout << "[obj] " << objFile.getFileName() << " has no triangle geometry." << std::endl;
        return false;
    }

    primitive.materialIndex = materialIndex;
    outModel.primitives.push_back(std::move(primitive));
    return true;
}

} // namespace ce
