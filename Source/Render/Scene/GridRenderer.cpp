#include "Render/Scene/GridRenderer.h"

using namespace juce::gl;

namespace ce {

namespace {
struct GridVertex {
    float position[3];
};
} // namespace

void GridRenderer::Build(float halfSize, float step) {
    std::vector<GridVertex> vertices;

    for (float x = -halfSize; x <= halfSize + step * 0.5f; x += step) {
        vertices.push_back({ { x, 0.0f, -halfSize } });
        vertices.push_back({ { x, 0.0f, halfSize } });
    }
    for (float z = -halfSize; z <= halfSize + step * 0.5f; z += step) {
        vertices.push_back({ { -halfSize, 0.0f, z } });
        vertices.push_back({ { halfSize, 0.0f, z } });
    }

    vertexBuffer_.Upload(GL_ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(GridVertex));

    vertexArray_.Bind();
    vertexBuffer_.Bind(GL_ARRAY_BUFFER);
    vertexArray_.SetAttribute(0, 3, sizeof(GridVertex), 0);
    gl::VertexArray::Unbind();

    vertexCount_ = static_cast<GLsizei>(vertices.size());
}

void GridRenderer::Draw() {
    vertexArray_.Bind();
    glDrawArrays(GL_LINES, 0, vertexCount_);
    gl::VertexArray::Unbind();
}

} // namespace ce
