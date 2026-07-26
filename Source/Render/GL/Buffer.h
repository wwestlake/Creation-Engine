#pragma once

#include <JuceHeader.h>

namespace ce::gl {

// RAII wrapper around a single OpenGL buffer object (VBO or EBO — the
// target you bind it to decides which). Assumes an OpenGL context is
// current for the lifetime of every call, same as the rest of this render
// layer.
class Buffer final {
public:
    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    // target: GL_ARRAY_BUFFER for vertex data, GL_ELEMENT_ARRAY_BUFFER for indices.
    void Upload(GLenum target, const void* data, std::size_t sizeBytes,
                GLenum usage = juce::gl::GL_STATIC_DRAW);
    void Bind(GLenum target) const;

    GLuint Id() const { return id_; }

private:
    void Release();

    GLuint id_ = 0;
};

} // namespace ce::gl
