#include "Render/ViewportComponent.h"

#include <array>
#include <iostream>

#include "Scene/Components.h"

using namespace juce::gl;

namespace {

void LogGLErrors(const char* where) {
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        std::cout << "[render] GL error 0x" << std::hex << err << std::dec << " at " << where << std::endl;
    }
}

// Upper-left 3x3 of a 4x4 column-major matrix, as a 9-float column-major
// array for setUniformMat3. Correct as a normal matrix for rotation and
// UNIFORM scale (any uniform scale factor cancels out under the shader's
// normalize() anyway) — not for non-uniform scale, which would need a
// proper inverse-transpose. Fine today since nothing sets a non-uniform
// scale yet; revisit if/when Transform editing (SC4) lets a designer do
// that.
std::array<float, 9> ExtractUpperLeft3x3(const juce::Matrix3D<float>& m) {
    return { m.mat[0], m.mat[1], m.mat[2], m.mat[4], m.mat[5], m.mat[6], m.mat[8], m.mat[9], m.mat[10] };
}

} // namespace

namespace ce {

ViewportComponent::ViewportComponent(engine::World& world) : world_(world), freeCamera_(*this) {
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
    auto view = world_.Registry().view<scene::MeshRenderer>();
    for (auto entity : view) {
        if (auto& material = view.get<scene::MeshRenderer>(entity).material) {
            material->roughness = value;
        }
    }
}

void ViewportComponent::SetMetallic(float value) {
    const juce::ScopedLock lock(stateLock_);
    auto view = world_.Registry().view<scene::MeshRenderer>();
    for (auto entity : view) {
        if (auto& material = view.get<scene::MeshRenderer>(entity).material) {
            material->metallic = value;
        }
    }
}

float ViewportComponent::Roughness() const {
    const juce::ScopedLock lock(stateLock_);
    auto view = world_.Registry().view<const scene::MeshRenderer>();
    for (auto entity : view) {
        if (const auto& material = view.get<const scene::MeshRenderer>(entity).material) {
            return material->roughness;
        }
    }
    return 0.4f;
}

float ViewportComponent::Metallic() const {
    const juce::ScopedLock lock(stateLock_);
    auto view = world_.Registry().view<const scene::MeshRenderer>();
    for (auto entity : view) {
        if (const auto& material = view.get<const scene::MeshRenderer>(entity).material) {
            return material->metallic;
        }
    }
    return 0.2f;
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

void ViewportComponent::SeedDemoScene() {
    if (hasSeededDemoScene_) {
        return;
    }
    hasSeededDemoScene_ = true;

    const auto* asset = assetCatalog_.Find("BoxTextured");
    if (asset == nullptr) {
        std::cout << "[scene] BoxTextured not in catalog; demo scene will be empty." << std::endl;
        return;
    }

    const auto entity = world_.CreateEntity();
    world_.Registry().emplace<scene::Name>(entity, scene::Name{ "Box" });
    world_.Registry().emplace<scene::Transform>(entity, scene::Transform{});
    world_.Registry().emplace<scene::MeshRenderer>(entity, scene::MeshRenderer{ asset->mesh, asset->material });
    demoEntity_ = entity;

    std::cout << "[scene] seeded demo entity 'Box'" << std::endl;
}

void ViewportComponent::ResetDemoEntityTransform() {
    const juce::ScopedLock lock(stateLock_);
    if (demoEntity_ == entt::null || !world_.Registry().valid(demoEntity_)) {
        return;
    }
    world_.Registry().get<scene::Transform>(demoEntity_) = scene::Transform{};
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
    // CesiumLogoFlat.png, mounted at higher priority than base.zip.
    const juce::File overrideFile = packagesDir.getChildFile("override.zip");
    if (overrideFile.existsAsFile()) {
        vfs_.Mount(overrideFile, 10);
    }

    assetCatalog_.LoadBuiltins(vfs_);
    SeedDemoScene();

    gridRenderer_.Build(10.0f, 1.0f);
    gridProgram_ = shaderComposer_->GetProgram(openGLContext_, "programs/grid.vert", "programs/grid.frag");

    LogGLErrors("catalog load + scene seed + grid");

    lastFrameTimeSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::cout << "[render] newOpenGLContextCreated: done" << std::endl;
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

    if (shaderComposer_ == nullptr) {
        return;
    }

    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const float deltaSeconds = static_cast<float>(nowSeconds - lastFrameTimeSeconds_);
    lastFrameTimeSeconds_ = nowSeconds;
    freeCamera_.Update(deltaSeconds);

    const float aspect = getHeight() > 0 ? static_cast<float>(getWidth()) / static_cast<float>(getHeight()) : 1.0f;
    camera_.SetPerspective(juce::MathConstants<float>::pi / 4.0f, aspect, 0.1f, 100.0f);
    const auto cameraPos = freeCamera_.Position();
    camera_.SetLookAt(cameraPos, freeCamera_.Target());

    if (gridProgram_ != nullptr) {
        gridProgram_->use();
        gridProgram_->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
        gridProgram_->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
        gridProgram_->setUniform("uColor", 0.2f, 0.24f, 0.32f);
        gridRenderer_.Draw();
    }

    // Snapshot the UI-editable light state once per frame under the lock,
    // then do all the (fast, non-blocking) GL uniform work below without
    // holding it — keeps message-thread edits from ever blocking on a
    // render that's mid-frame, and vice versa.
    DirectionalLight sunLightSnapshot;
    std::vector<PointLight> pointLightsSnapshot;
    {
        const juce::ScopedLock lock(stateLock_);
        sunLightSnapshot = sunLight_;
        pointLightsSnapshot = pointLights_;
    }

    // Demo-only: spins every placed entity's Transform based on the
    // World's tick count rather than wall-clock time, so the spin
    // actually stops when the transport is paused/stopped (World::
    // AdvanceTick() is gated on play state in MainComponent's timer) —
    // an editor "playing" a paused simulation should look paused.
    // Locked because ResetDemoEntityTransform() (message thread, on Stop)
    // writes the same Transform this loop writes from the render thread.
    {
        const juce::ScopedLock lock(stateLock_);
        const float spinRadians = 0.5f * (static_cast<float>(world_.CurrentTick()) / 30.0f);
        auto view = world_.Registry().view<scene::Transform>();
        for (auto entity : view) {
            view.get<scene::Transform>(entity).eulerRotationRadians.y = spinRadians;
        }
    }

    auto drawView = world_.Registry().view<const scene::Transform, const scene::MeshRenderer>();
    for (auto entity : drawView) {
        const auto& transform = drawView.get<const scene::Transform>(entity);
        const auto& renderer = drawView.get<const scene::MeshRenderer>(entity);
        if (!renderer.mesh || !renderer.material) {
            continue;
        }

        auto* program = renderer.material->Resolve(*shaderComposer_, openGLContext_);
        if (program == nullptr) {
            continue;
        }

        const auto model = transform.ToModelMatrix();
        const auto normalMatrix = ExtractUpperLeft3x3(model);

        program->use();
        program->setUniformMat4("uModel", model.mat, 1, GL_FALSE);
        program->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
        program->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
        program->setUniformMat3("uNormalMatrix", normalMatrix.data(), 1, GL_FALSE);
        program->setUniform("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);

        {
            const juce::ScopedLock lock(stateLock_);
            renderer.material->ApplyUniforms(*program);
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

        renderer.mesh->Draw();
    }

    LogGLErrors("draw");
}

void ViewportComponent::openGLContextClosing() {
    auto view = world_.Registry().view<scene::MeshRenderer>();
    for (auto entity : view) {
        if (auto& material = view.get<scene::MeshRenderer>(entity).material) {
            material->InvalidateCache();
        }
    }
    gridProgram_ = nullptr;
    shaderComposer_.reset();
}

void ViewportComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    freeCamera_.AdjustSpeed(wheel.deltaY);
}

} // namespace ce
