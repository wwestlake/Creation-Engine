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

} // namespace ce
