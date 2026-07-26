#include "Render/GL/VertexArray.h"

using namespace juce::gl;

namespace ce::gl {

VertexArray::~VertexArray() {
    Release();
}

VertexArray::VertexArray(VertexArray&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        Release();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void VertexArray::Release() {
    if (id_ != 0) {
        glDeleteVertexArrays(1, &id_);
        id_ = 0;
    }
}

void VertexArray::EnsureCreated() {
    if (id_ == 0) {
        glGenVertexArrays(1, &id_);
    }
}

void VertexArray::Bind() {
    EnsureCreated();
    glBindVertexArray(id_);
}

void VertexArray::Unbind() {
    glBindVertexArray(0);
}

void VertexArray::SetAttribute(GLuint index, int componentCount, GLsizei strideBytes, std::size_t offsetBytes) {
    Bind();
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, componentCount, GL_FLOAT, GL_FALSE, strideBytes,
                           reinterpret_cast<const void*>(offsetBytes));
}

} // namespace ce::gl
