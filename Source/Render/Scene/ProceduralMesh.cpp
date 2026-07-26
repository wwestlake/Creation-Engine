#include "Render/Scene/ProceduralMesh.h"

#include <cmath>

namespace ce {

void GenerateUVSphere(int rings, int segments, std::vector<Vertex>& outVertices, std::vector<GLuint>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    const float pi = juce::MathConstants<float>::pi;

    for (int ring = 0; ring <= rings; ++ring) {
        const float phi = pi * static_cast<float>(ring) / static_cast<float>(rings);
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);

        for (int segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            const float x = ringRadius * std::cos(theta);
            const float z = ringRadius * std::sin(theta);

            // Unit sphere: position and outward normal are the same vector.
            Vertex vertex{};
            vertex.position[0] = x;
            vertex.position[1] = y;
            vertex.position[2] = z;
            vertex.normal[0] = x;
            vertex.normal[1] = y;
            vertex.normal[2] = z;
            vertex.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            vertex.uv[1] = static_cast<float>(ring) / static_cast<float>(rings);
            outVertices.push_back(vertex);
        }
    }

    const int rowStride = segments + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const GLuint a = static_cast<GLuint>(ring * rowStride + segment);
            const GLuint b = static_cast<GLuint>(a + rowStride);

            outIndices.push_back(a);
            outIndices.push_back(b);
            outIndices.push_back(a + 1);

            outIndices.push_back(a + 1);
            outIndices.push_back(b);
            outIndices.push_back(b + 1);
        }
    }
}

namespace {
struct Vec3 {
    float x, y, z;
};

Vec3 Add(Vec3 a, Vec3 b, float bScale) {
    return { a.x + b.x * bScale, a.y + b.y * bScale, a.z + b.z * bScale };
}
} // namespace

void GenerateCube(std::vector<Vertex>& outVertices, std::vector<GLuint>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    // Per face: outward normal N, and in-face tangent axes U/V chosen so
    // U x V == N — that guarantees consistent CCW winding (viewed from
    // outside) across all six faces from one shared index pattern below,
    // rather than having to special-case winding per face.
    struct Face {
        Vec3 n, u, v;
    };
    const Face faces[6] = {
        { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },   // +Z
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } }, // -Z
        { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },  // +X
        { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },  // -X
        { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },  // +Y
        { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },  // -Y
    };

    for (const auto& face : faces) {
        const Vec3 center = { face.n.x * 0.5f, face.n.y * 0.5f, face.n.z * 0.5f };
        const Vec3 corners[4] = {
            Add(Add(center, face.u, -0.5f), face.v, -0.5f), // bottom-left
            Add(Add(center, face.u, 0.5f), face.v, -0.5f),  // bottom-right
            Add(Add(center, face.u, -0.5f), face.v, 0.5f),  // top-left
            Add(Add(center, face.u, 0.5f), face.v, 0.5f),   // top-right
        };
        const float uvs[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };

        const auto baseIndex = static_cast<GLuint>(outVertices.size());
        for (int i = 0; i < 4; ++i) {
            Vertex vertex{};
            vertex.position[0] = corners[i].x;
            vertex.position[1] = corners[i].y;
            vertex.position[2] = corners[i].z;
            vertex.normal[0] = face.n.x;
            vertex.normal[1] = face.n.y;
            vertex.normal[2] = face.n.z;
            vertex.uv[0] = uvs[i][0];
            vertex.uv[1] = uvs[i][1];
            outVertices.push_back(vertex);
        }

        // BL, BR, TL / TR, TL, BR — CCW given U x V == N (see Face table above).
        outIndices.push_back(baseIndex + 0);
        outIndices.push_back(baseIndex + 1);
        outIndices.push_back(baseIndex + 2);
        outIndices.push_back(baseIndex + 3);
        outIndices.push_back(baseIndex + 2);
        outIndices.push_back(baseIndex + 1);
    }
}

} // namespace ce
