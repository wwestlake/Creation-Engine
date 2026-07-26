#include "Render/ViewportComponent.h"

#include <array>
#include <iostream>

#include "Render/Import/GltfLoader.h"

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

void ViewportComponent::SetRoughness(float value) {
    for (auto& material : materials_) {
        material.roughness = value;
    }
}

void ViewportComponent::SetMetallic(float value) {
    for (auto& material : materials_) {
        material.metallic = value;
    }
}

float ViewportComponent::Roughness() const {
    return materials_.empty() ? 0.4f : materials_.front().roughness;
}

float ViewportComponent::Metallic() const {
    return materials_.empty() ? 0.2f : materials_.front().metallic;
}

void ViewportComponent::newOpenGLContextCreated() {
    std::cout << "[render] newOpenGLContextCreated: GL_VERSION="
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

    shaderComposer_ = std::make_unique<ShaderComposer>(juce::File(CE_SHADER_SOURCE_DIR));

    // Proves the composer handles more than one program/variant: the
    // unlit debug program is compiled and cached here even though the
    // demo scene below only draws through PBR materials.
    shaderComposer_->GetProgram(openGLContext_, "programs/unlit.vert", "programs/unlit.frag");

    const juce::File gltfFile =
        juce::File(CE_ASSET_SOURCE_DIR).getChildFile("models/BoxTextured/BoxTextured.gltf");

    LoadedModel model;
    if (!LoadGltf(gltfFile, model)) {
        std::cout << "[render] glTF load failed; nothing will render." << std::endl;
        return;
    }

    for (const auto& srcMaterial : model.materials) {
        Material material;
        material.albedo = srcMaterial.baseColorFactor;
        material.metallic = srcMaterial.metallicFactor;
        material.roughness = srcMaterial.roughnessFactor;

        if (srcMaterial.baseColorTexturePath.existsAsFile()) {
            auto texture = std::make_unique<gl::Texture2D>();
            if (texture->LoadFromFile(srcMaterial.baseColorTexturePath)) {
                material.albedoTexture = texture.get();
            }
            loadedTextures_.push_back(std::move(texture));
        }

        materials_.push_back(std::move(material));
    }
    if (materials_.empty()) {
        materials_.emplace_back(); // safety net: keep Roughness()/Metallic()/rendering well-defined.
    }

    meshes_.reserve(model.primitives.size());
    primitiveMaterialIndex_.reserve(model.primitives.size());
    for (const auto& primitive : model.primitives) {
        Mesh mesh;
        mesh.Upload(primitive.vertices, primitive.indices);
        meshes_.push_back(std::move(mesh));

        const bool validIndex = primitive.materialIndex >= 0 &&
                                 primitive.materialIndex < static_cast<int>(model.materials.size());
        primitiveMaterialIndex_.push_back(validIndex ? primitive.materialIndex : 0);
    }
    LogGLErrors("model load + upload");

    glEnable(GL_DEPTH_TEST);

    startTimeSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::cout << "[render] newOpenGLContextCreated: done (" << meshes_.size() << " mesh(es))" << std::endl;
}

void ViewportComponent::renderOpenGL() {
    const auto scale = static_cast<float>(openGLContext_.getRenderingScale());
    glViewport(0, 0, juce::roundToInt(scale * static_cast<float>(getWidth())),
               juce::roundToInt(scale * static_cast<float>(getHeight())));

    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (meshes_.empty()) {
        return;
    }

    const float aspect = getHeight() > 0 ? static_cast<float>(getWidth()) / static_cast<float>(getHeight()) : 1.0f;
    camera_.SetPerspective(juce::MathConstants<float>::pi / 4.0f, aspect, 0.1f, 100.0f);
    const juce::Vector3D<float> cameraPos{ 0.0f, 0.8f, 2.5f };
    camera_.SetLookAt(cameraPos, { 0.0f, 0.0f, 0.0f });

    const double elapsedSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - startTimeSeconds_;
    const auto model = juce::Matrix3D<float>::rotation({ 0.0f, 0.5f * static_cast<float>(elapsedSeconds), 0.0f });
    const auto normalMatrix = ExtractUpperLeft3x3(model);

    for (std::size_t i = 0; i < meshes_.size(); ++i) {
        Material& material = materials_[static_cast<std::size_t>(primitiveMaterialIndex_[i])];
        auto* program = material.Resolve(*shaderComposer_, openGLContext_);
        if (program == nullptr) {
            continue;
        }

        program->use();
        program->setUniformMat4("uModel", model.mat, 1, GL_FALSE);
        program->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
        program->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
        program->setUniformMat3("uNormalMatrix", normalMatrix.data(), 1, GL_FALSE);
        program->setUniform("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);

        material.ApplyUniforms(*program);

        // Hardcoded lights for this milestone — M5 adds an editable light list.
        program->setUniform("uSunLight.direction", -0.4f, -1.0f, -0.3f);
        program->setUniform("uSunLight.color", 1.0f, 0.97f, 0.9f);
        program->setUniform("uSunLight.intensity", 3.0f);

        program->setUniform("uPointLight.position", 2.0f, 1.5f, 2.0f);
        program->setUniform("uPointLight.color", 0.3f, 0.5f, 1.0f);
        program->setUniform("uPointLight.intensity", 8.0f);

        meshes_[i].Draw();
    }

    LogGLErrors("draw");
}

void ViewportComponent::openGLContextClosing() {
    for (auto& material : materials_) {
        material.InvalidateCache();
    }
    shaderComposer_.reset();
}

} // namespace ce
