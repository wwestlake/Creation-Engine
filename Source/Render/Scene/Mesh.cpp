#include "Render/Scene/Mesh.h"

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
    gl::VertexArray::Unbind();

    indexCount_ = static_cast<GLsizei>(indices.size());
}

void Mesh::Draw() {
    vertexArray_.Bind();
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    gl::VertexArray::Unbind();
}

} // namespace ce
