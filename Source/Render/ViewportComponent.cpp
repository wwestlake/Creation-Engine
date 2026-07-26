#include "Render/ViewportComponent.h"

#include <iostream>

using namespace juce::gl;

namespace {

// M1 placeholder shaders: vertex-colored geometry, no lighting yet.
// M2 replaces these with programs assembled by the shader composer.
constexpr const char* kVertexSource = R"(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentSource = R"(
#version 410 core
in vec3 vColor;
out vec4 fragColor;

void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

struct Vertex {
    float position[3];
    float color[3];
};

// clang-format off
constexpr Vertex kCubeVertices[8] = {
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 1.0f } },
};

constexpr GLuint kCubeIndices[36] = {
    0, 1, 2,  2, 3, 0, // -Z
    4, 5, 6,  6, 7, 4, // +Z
    0, 3, 7,  7, 4, 0, // -X
    1, 5, 6,  6, 2, 1, // +X
    0, 4, 5,  5, 1, 0, // -Y
    3, 2, 6,  6, 7, 3, // +Y
};
// clang-format on

} // namespace

namespace ce {

ViewportComponent::ViewportComponent() {
    openGLContext_.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1);
    openGLContext_.setRenderer(this);
    openGLContext_.attachTo(*this);
    openGLContext_.setContinuousRepainting(true);
}

ViewportComponent::~ViewportComponent() {
    openGLContext_.detach();
}

namespace {
void LogGLErrors(const char* where) {
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        std::cout << "[render] GL error 0x" << std::hex << err << std::dec << " at " << where << std::endl;
    }
}
} // namespace

void ViewportComponent::newOpenGLContextCreated() {
    std::cout << "[render] newOpenGLContextCreated: GL_VERSION="
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(openGLContext_);
    if (!shader_->addVertexShader(kVertexSource) || !shader_->addFragmentShader(kFragmentSource) ||
        !shader_->link()) {
        std::cout << "[render] shader compile/link failed: " << shader_->getLastError() << std::endl;
        shader_.reset();
        return;
    }
    LogGLErrors("shader link");

    vertexBuffer_.Upload(GL_ARRAY_BUFFER, kCubeVertices, sizeof(kCubeVertices));
    indexBuffer_.Upload(GL_ELEMENT_ARRAY_BUFFER, kCubeIndices, sizeof(kCubeIndices));
    LogGLErrors("buffer upload");

    vertexArray_.Bind();
    vertexBuffer_.Bind(GL_ARRAY_BUFFER);
    indexBuffer_.Bind(GL_ELEMENT_ARRAY_BUFFER);
    vertexArray_.SetAttribute(0, 3, sizeof(Vertex), offsetof(Vertex, position));
    vertexArray_.SetAttribute(1, 3, sizeof(Vertex), offsetof(Vertex, color));
    gl::VertexArray::Unbind();
    LogGLErrors("vertex array setup");

    glEnable(GL_DEPTH_TEST);

    startTimeSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::cout << "[render] newOpenGLContextCreated: done" << std::endl;
}

void ViewportComponent::renderOpenGL() {
    const auto scale = static_cast<float>(openGLContext_.getRenderingScale());
    glViewport(0, 0, juce::roundToInt(scale * static_cast<float>(getWidth())),
               juce::roundToInt(scale * static_cast<float>(getHeight())));

    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (shader_ == nullptr) {
        return;
    }

    const float aspect = getHeight() > 0 ? static_cast<float>(getWidth()) / static_cast<float>(getHeight()) : 1.0f;
    camera_.SetPerspective(juce::MathConstants<float>::pi / 4.0f, aspect, 0.1f, 100.0f);
    camera_.SetLookAt({ 0.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 0.0f });

    const double elapsedSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - startTimeSeconds_;
    const auto model =
        juce::Matrix3D<float>::rotation({ 0.4f * static_cast<float>(elapsedSeconds),
                                           0.7f * static_cast<float>(elapsedSeconds), 0.0f });

    shader_->use();
    shader_->setUniformMat4("uModel", model.mat, 1, GL_FALSE);
    shader_->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
    shader_->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);

    vertexArray_.Bind();
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    gl::VertexArray::Unbind();
    LogGLErrors("draw");
}

void ViewportComponent::openGLContextClosing() {
    shader_.reset();
}

} // namespace ce
