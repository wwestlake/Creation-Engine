#pragma once

#include <JuceHeader.h>

namespace ce::gl {

// RAII wrapper around a Vertex Array Object: the binding of vertex
// attribute layouts (positions, normals, UVs, ...) to buffer data that a
// draw call replays. Callers bind the source VBO/EBO themselves before
// calling SetAttribute, matching plain OpenGL usage.
class VertexArray final {
public:
    VertexArray() = default;
    ~VertexArray();

    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;

    // Lazily creates the underlying VAO on first call, so callers can
    // Bind() before any SetAttribute() and still have EBO/attribute state
    // recorded onto the real object instead of the default (id 0) one.
    void Bind();
    static void Unbind();

    // Configures attribute `index` to read `componentCount` floats per
    // vertex, starting at `offsetBytes` within each `strideBytes`-wide
    // vertex record of the currently-bound GL_ARRAY_BUFFER.
    void SetAttribute(GLuint index, int componentCount, GLsizei strideBytes, std::size_t offsetBytes);

    GLuint Id() const { return id_; }

private:
    void EnsureCreated();
    void Release();

    GLuint id_ = 0;
};

} // namespace ce::gl
