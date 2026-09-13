#include "Render/Scene/Mesh.h"

#include <limits>

using namespace juce::gl;

namespace ce {

void Mesh::Upload(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices) {
    vertexBuffer_.Upload(GL_ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(Vertex));
    indexBuffer_.Upload(GL_ELEMENT_ARRAY_BUFFER, indices.data(), indices.size() * sizeof(GLuint));

    vertexArray_.Bind();
    vertexBuffer_.Bind(GL_ARRAY_BUFFER);
    indexBuffer_.Bind(GL_ELEMENT_ARRAY_BUFFER);
    vertexArray_.SetAttribute(0, 3, sizeof(Vertex), offsetof(Vertex, position));
    vertexArray_.SetAttribute(1, 3, sizeof(Vertex), offsetof(Vertex, normal));
    vertexArray_.SetAttribute(2, 2, sizeof(Vertex), offsetof(Vertex, uv));
    // Bound for every mesh, skinned or not -- harmless for the ordinary
    // PBR/unlit shaders, which never declare locations 3/4, and means
    // AI5's skinning shader variant can start reading real per-vertex
    // bone data the moment it exists without any Mesh-side changes.
    vertexArray_.SetAttribute(3, 4, sizeof(Vertex), offsetof(Vertex, boneIndices));
    vertexArray_.SetAttribute(4, 4, sizeof(Vertex), offsetof(Vertex, boneWeights));
    gl::VertexArray::Unbind();

    indexCount_ = static_cast<GLsizei>(indices.size());

    if (vertices.empty()) {
        bounds_ = {};
        return;
    }

    const auto largest = std::numeric_limits<float>::max();
    bounds_.minimum = { largest, largest, largest };
    bounds_.maximum = { -largest, -largest, -largest };
    for (const auto& vertex : vertices) {
        bounds_.minimum.x = juce::jmin(bounds_.minimum.x, vertex.position[0]);
        bounds_.minimum.y = juce::jmin(bounds_.minimum.y, vertex.position[1]);
        bounds_.minimum.z = juce::jmin(bounds_.minimum.z, vertex.position[2]);
        bounds_.maximum.x = juce::jmax(bounds_.maximum.x, vertex.position[0]);
        bounds_.maximum.y = juce::jmax(bounds_.maximum.y, vertex.position[1]);
        bounds_.maximum.z = juce::jmax(bounds_.maximum.z, vertex.position[2]);
    }
}

void Mesh::Draw() {
    vertexArray_.Bind();
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    gl::VertexArray::Unbind();
}

} // namespace ce
