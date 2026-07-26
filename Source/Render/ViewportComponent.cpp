#include "Render/ViewportComponent.h"

#include <array>
#include <iostream>

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

    pbrProgram_ = shaderComposer_->GetProgram(openGLContext_, "programs/pbr_lit.vert", "programs/pbr_lit.frag");
    unlitProgram_ = shaderComposer_->GetProgram(openGLContext_, "programs/unlit.vert", "programs/unlit.frag");
    // Fetching the same PBR program again proves the cache path (should log a hit, not a recompile).
    shaderComposer_->GetProgram(openGLContext_, "programs/pbr_lit.vert", "programs/pbr_lit.frag");

    if (pbrProgram_ == nullptr) {
        std::cout << "[render] PBR program failed to compile; nothing will render." << std::endl;
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

    if (pbrProgram_ == nullptr) {
        return;
    }

    const float aspect = getHeight() > 0 ? static_cast<float>(getWidth()) / static_cast<float>(getHeight()) : 1.0f;
    camera_.SetPerspective(juce::MathConstants<float>::pi / 4.0f, aspect, 0.1f, 100.0f);
    const juce::Vector3D<float> cameraPos{ 0.0f, 0.5f, 3.0f };
    camera_.SetLookAt(cameraPos, { 0.0f, 0.0f, 0.0f });

    const double elapsedSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - startTimeSeconds_;
    const auto model = juce::Matrix3D<float>::rotation({ 0.0f, 0.5f * static_cast<float>(elapsedSeconds), 0.0f });
    const auto normalMatrix = ExtractUpperLeft3x3(model);

    pbrProgram_->use();
    pbrProgram_->setUniformMat4("uModel", model.mat, 1, GL_FALSE);
    pbrProgram_->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
    pbrProgram_->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
    pbrProgram_->setUniformMat3("uNormalMatrix", normalMatrix.data(), 1, GL_FALSE);
    pbrProgram_->setUniform("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);

    // Hardcoded material + lights for this milestone — M3 introduces a
    // real Material type, M5 an editable light list.
    pbrProgram_->setUniform("uAlbedo", 0.7f, 0.15f, 0.15f);
    pbrProgram_->setUniform("uMetallic", 0.2f);
    pbrProgram_->setUniform("uRoughness", 0.4f);

    pbrProgram_->setUniform("uSunLight.direction", -0.4f, -1.0f, -0.3f);
    pbrProgram_->setUniform("uSunLight.color", 1.0f, 0.97f, 0.9f);
    pbrProgram_->setUniform("uSunLight.intensity", 3.0f);

    pbrProgram_->setUniform("uPointLight.position", 2.0f, 1.5f, 2.0f);
    pbrProgram_->setUniform("uPointLight.color", 0.3f, 0.5f, 1.0f);
    pbrProgram_->setUniform("uPointLight.intensity", 8.0f);

    sphereMesh_.Draw();
    LogGLErrors("draw");
}

void ViewportComponent::openGLContextClosing() {
    pbrProgram_ = nullptr;
    unlitProgram_ = nullptr;
    shaderComposer_.reset();
}

} // namespace ce
