#include "Render/GL/Buffer.h"

using namespace juce::gl;

namespace ce::gl {

Buffer::~Buffer() {
    Release();
}

Buffer::Buffer(Buffer&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        Release();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void Buffer::Release() {
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
        id_ = 0;
    }
}

void Buffer::Upload(GLenum target, const void* data, std::size_t sizeBytes, GLenum usage) {
    if (id_ == 0) {
        glGenBuffers(1, &id_);
    }
    glBindBuffer(target, id_);
    glBufferData(target, static_cast<GLsizeiptr>(sizeBytes), data, usage);
}

void Buffer::Bind(GLenum target) const {
    glBindBuffer(target, id_);
}

} // namespace ce::gl
