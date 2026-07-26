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
    const juce::ScopedLock lock(stateLock_);
    for (auto& material : materials_) {
        material.roughness = value;
    }
}

void ViewportComponent::SetMetallic(float value) {
    const juce::ScopedLock lock(stateLock_);
    for (auto& material : materials_) {
        material.metallic = value;
    }
}

float ViewportComponent::Roughness() const {
    const juce::ScopedLock lock(stateLock_);
    return materials_.empty() ? 0.4f : materials_.front().roughness;
}

float ViewportComponent::Metallic() const {
    const juce::ScopedLock lock(stateLock_);
    return materials_.empty() ? 0.2f : materials_.front().metallic;
}

DirectionalLight ViewportComponent::GetSunLight() const {
    const juce::ScopedLock lock(stateLock_);
    return sunLight_;
}

void ViewportComponent::SetSunLight(const DirectionalLight& light) {
    const juce::ScopedLock lock(stateLock_);
    sunLight_ = light;
}

int ViewportComponent::GetPointLightCount() const {
    const juce::ScopedLock lock(stateLock_);
    return static_cast<int>(pointLights_.size());
}

PointLight ViewportComponent::GetPointLight(int index) const {
    const juce::ScopedLock lock(stateLock_);
    if (index < 0 || index >= static_cast<int>(pointLights_.size())) {
        return {};
    }
    return pointLights_[static_cast<std::size_t>(index)];
}

void ViewportComponent::SetPointLight(int index, const PointLight& light) {
    const juce::ScopedLock lock(stateLock_);
    if (index < 0 || index >= static_cast<int>(pointLights_.size())) {
        return;
    }
    pointLights_[static_cast<std::size_t>(index)] = light;
}

void ViewportComponent::AddPointLight() {
    const juce::ScopedLock lock(stateLock_);
    if (static_cast<int>(pointLights_.size()) >= kMaxPointLights) {
        return;
    }
    pointLights_.push_back(PointLight{});
}

void ViewportComponent::RemovePointLight(int index) {
    const juce::ScopedLock lock(stateLock_);
    if (index < 0 || index >= static_cast<int>(pointLights_.size())) {
        return;
    }
    pointLights_.erase(pointLights_.begin() + index);
}

void ViewportComponent::newOpenGLContextCreated() {
    std::cout << "[render] newOpenGLContextCreated: GL_VERSION="
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

    shaderComposer_ = std::make_unique<ShaderComposer>(juce::File(CE_SHADER_SOURCE_DIR));

    // Proves the composer handles more than one program/variant: the
    // unlit debug program is compiled and cached here even though the
    // demo scene below only draws through PBR materials.
    shaderComposer_->GetProgram(openGLContext_, "programs/unlit.vert", "programs/unlit.frag");

    const juce::File packagesDir = juce::File(CE_ASSET_SOURCE_DIR).getChildFile("packages");
    if (!vfs_.Mount(packagesDir.getChildFile("base.zip"), 0)) {
        std::cout << "[render] failed to mount base.zip; nothing will render." << std::endl;
        return;
    }
    // Mount-priority demo: override.zip only contains a replacement
    // CesiumLogoFlat.png, mounted at higher priority than base.zip. The
    // VFS should resolve that path to override.zip's copy instead of
    // base.zip's — verified by the differing byte size/dimensions
    // VirtualFileSystem::ReadFile and Texture2D log for that read.
    const juce::File overrideFile = packagesDir.getChildFile("override.zip");
    if (overrideFile.existsAsFile()) {
        vfs_.Mount(overrideFile, 10);
    }

    LoadedModel model;
    if (!LoadGltfFromVfs(vfs_, "BoxTextured.gltf", model)) {
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
        } else if (srcMaterial.baseColorTextureVirtualPath.isNotEmpty()) {
            juce::MemoryBlock textureBytes;
            if (vfs_.ReadFile(srcMaterial.baseColorTextureVirtualPath, textureBytes)) {
                auto texture = std::make_unique<gl::Texture2D>();
                if (texture->LoadFromMemory(textureBytes.getData(), textureBytes.getSize(),
                                             srcMaterial.baseColorTextureVirtualPath)) {
                    material.albedoTexture = texture.get();
                }
                loadedTextures_.push_back(std::move(texture));
            }
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

    startTimeSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::cout << "[render] newOpenGLContextCreated: done (" << meshes_.size() << " mesh(es))" << std::endl;
}

void ViewportComponent::renderOpenGL() {
    // Must be set every frame, not once at context creation: JUCE's own
    // OpenGLGraphicsContext composites this component's 2D paint() on the
    // same context immediately after this callback returns each frame,
    // and it unconditionally calls glDisable(GL_DEPTH_TEST) as part of
    // its own setup (juce_OpenGLGraphicsContext.cpp). Enabling depth
    // testing once at startup meant frame 1 was correct and every frame
    // after it silently rendered with no depth test at all — triangles
    // compositing in raw index-buffer order instead of front-to-back, so
    // back faces of a rotating mesh would intermittently paint over
    // front ones. Culling is enabled alongside it for the same reason:
    // both are baseline state for opaque closed meshes, and leaving
    // either unset invites this class of bug back.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

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

    // Snapshot the UI-editable state once per frame under the lock, then
    // do all the (fast, non-blocking) GL uniform work below without
    // holding it — keeps message-thread edits from ever blocking on a
    // render that's mid-frame, and vice versa.
    DirectionalLight sunLightSnapshot;
    std::vector<PointLight> pointLightsSnapshot;
    {
        const juce::ScopedLock lock(stateLock_);
        sunLightSnapshot = sunLight_;
        pointLightsSnapshot = pointLights_;
    }

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

        {
            const juce::ScopedLock lock(stateLock_);
            material.ApplyUniforms(*program);
        }

        program->setUniform("uSunLight.direction", sunLightSnapshot.direction.x, sunLightSnapshot.direction.y,
                             sunLightSnapshot.direction.z);
        program->setUniform("uSunLight.color", sunLightSnapshot.color.x, sunLightSnapshot.color.y,
                             sunLightSnapshot.color.z);
        program->setUniform("uSunLight.intensity", sunLightSnapshot.intensity);

        const int pointLightCount = juce::jmin(static_cast<int>(pointLightsSnapshot.size()), kMaxPointLights);
        for (int lightIndex = 0; lightIndex < pointLightCount; ++lightIndex) {
            const auto& light = pointLightsSnapshot[static_cast<std::size_t>(lightIndex)];
            const juce::String prefix = "uPointLights[" + juce::String(lightIndex) + "].";
            program->setUniform((prefix + "position").toRawUTF8(), light.position.x, light.position.y,
                                 light.position.z);
            program->setUniform((prefix + "color").toRawUTF8(), light.color.x, light.color.y, light.color.z);
            program->setUniform((prefix + "intensity").toRawUTF8(), light.intensity);
        }
        program->setUniform("uPointLightCount", pointLightCount);

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
