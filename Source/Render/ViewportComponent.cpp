#include "Render/ViewportComponent.h"

#include <array>
#include <iostream>
#include <vector>

#include "Render/Scene/ProceduralMesh.h"

using namespace juce::gl;

namespace {

void LogGLErrors(const char* where) {
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        std::cout << "[render] GL error 0x" << std::hex << err << std::dec << " at " << where << std::endl;
    }
}

// Upper-left 3x3 of a 4x4 column-major matrix, as a 9-float column-major
// array for setUniformMat3. Correct as a normal matrix here because the
// model transform is rotation-only (orthonormal), so no inverse-transpose
// is needed — that requirement returns once non-uniform scale is possible.
std::array<float, 9> ExtractUpperLeft3x3(const juce::Matrix3D<float>& m) {
    return { m.mat[0], m.mat[1], m.mat[2], m.mat[4], m.mat[5], m.mat[6], m.mat[8], m.mat[9], m.mat[10] };
}

// Generates a simple RGB checker pattern in memory — a stand-in test
// texture until M4 loads real glTF-authored images through
// Texture2D::LoadFromFile (the stb_image-backed path, implemented but
// not yet exercised by this milestone's demo scene).
std::vector<std::uint8_t> GenerateCheckerPixels(int size, int checksPerSide) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int checkX = (x * checksPerSide) / size;
            const int checkY = (y * checksPerSide) / size;
            const bool light = (checkX + checkY) % 2 == 0;
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            pixels[i + 0] = light ? 235 : 60;
            pixels[i + 1] = light ? 235 : 60;
            pixels[i + 2] = light ? 235 : 70;
        }
    }
    return pixels;
}

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

void ViewportComponent::newOpenGLContextCreated() {
    std::cout << "[render] newOpenGLContextCreated: GL_VERSION="
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

    shaderComposer_ = std::make_unique<ShaderComposer>(juce::File(CE_SHADER_SOURCE_DIR));

    // Proves the composer handles more than one program/variant: the
    // unlit debug program is compiled and cached here even though the
    // demo scene below only draws through the PBR material.
    shaderComposer_->GetProgram(openGLContext_, "programs/unlit.vert", "programs/unlit.frag");

    const auto checkerPixels = GenerateCheckerPixels(64, 8);
    checkerTexture_.CreateFromPixels(64, 64, 3, checkerPixels.data());
    material_.albedoTexture = &checkerTexture_;

    if (material_.Resolve(*shaderComposer_, openGLContext_) == nullptr) {
        std::cout << "[render] PBR material failed to compile; nothing will render." << std::endl;
    }
    LogGLErrors("shader composition");

    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    GenerateUVSphere(24, 48, vertices, indices);
    sphereMesh_.Upload(vertices, indices);
    LogGLErrors("mesh upload");

    glEnable(GL_DEPTH_TEST);

    startTimeSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::cout << "[render] newOpenGLContextCreated: done" << std::endl;
}

void ViewportComponent::renderOpenGL() {
    const auto scale = static_cast<float>(openGLContext_.getRenderingScale());
    glViewport(0, 0, juce::roundToInt(scale * static_cast<float>(getWidth())),
               juce::roundToInt(scale * static_cast<float>(getHeight())));

    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto* program = material_.Resolve(*shaderComposer_, openGLContext_);
    if (program == nullptr) {
        return;
    }

    const float aspect = getHeight() > 0 ? static_cast<float>(getWidth()) / static_cast<float>(getHeight()) : 1.0f;
    camera_.SetPerspective(juce::MathConstants<float>::pi / 4.0f, aspect, 0.1f, 100.0f);
    const juce::Vector3D<float> cameraPos{ 0.0f, 0.5f, 3.0f };
    camera_.SetLookAt(cameraPos, { 0.0f, 0.0f, 0.0f });

    const double elapsedSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - startTimeSeconds_;
    const auto model = juce::Matrix3D<float>::rotation({ 0.0f, 0.5f * static_cast<float>(elapsedSeconds), 0.0f });
    const auto normalMatrix = ExtractUpperLeft3x3(model);

    program->use();
    program->setUniformMat4("uModel", model.mat, 1, GL_FALSE);
    program->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
    program->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
    program->setUniformMat3("uNormalMatrix", normalMatrix.data(), 1, GL_FALSE);
    program->setUniform("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);

    material_.ApplyUniforms(*program);

    // Hardcoded lights for this milestone — M5 adds an editable light list.
    program->setUniform("uSunLight.direction", -0.4f, -1.0f, -0.3f);
    program->setUniform("uSunLight.color", 1.0f, 0.97f, 0.9f);
    program->setUniform("uSunLight.intensity", 3.0f);

    program->setUniform("uPointLight.position", 2.0f, 1.5f, 2.0f);
    program->setUniform("uPointLight.color", 0.3f, 0.5f, 1.0f);
    program->setUniform("uPointLight.intensity", 8.0f);

    sphereMesh_.Draw();
    LogGLErrors("draw");
}

void ViewportComponent::openGLContextClosing() {
    material_.InvalidateCache();
    shaderComposer_.reset();
}

} // namespace ce
