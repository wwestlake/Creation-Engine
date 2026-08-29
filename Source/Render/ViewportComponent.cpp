#include "Render/ViewportComponent.h"

#include <array>
#include <cmath>
#include <iostream>
#include <mutex>

#include "engine/core_components.h"
#include "Render/Scene/Animation.h"
#include "Scene/AnimationSampler.h"
#include "Scene/Components.h"
#include "Scene/TransformHierarchy.h"

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
    setWantsKeyboardFocus(true);
    openGLContext_.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1);
    openGLContext_.setRenderer(this);
    openGLContext_.attachTo(*this);
    openGLContext_.setContinuousRepainting(true);
}

ViewportComponent::~ViewportComponent() {
    openGLContext_.detach();
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

    auto asset = assetCatalog_.Find("BoxTextured");
    if (asset.mesh == nullptr)
        asset = assetCatalog_.Find("Cube");
    if (asset.mesh == nullptr) {
        std::cout << "[scene] no cube asset in catalog; starter room will be empty." << std::endl;
        return;
    }

    const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto addBox = [&](const char* name, engine::Vec3 position, engine::Vec3 scale) {
        const auto entity = world_.CreateEntity();
        scene::Transform transform;
        transform.position = position;
        transform.scale = scale;
        world_.Registry().emplace<scene::Name>(entity, scene::Name{ name });
        world_.Registry().emplace<scene::Transform>(entity, transform);
        world_.Registry().emplace<scene::MeshRenderer>(entity, scene::MeshRenderer{ asset.mesh, asset.material });
        world_.Registry().emplace<scene::SceneFlags>(entity, scene::SceneFlags{});
        return entity;
    };

    demoEntity_ = addBox("Center Block", { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f });
    addBox("Floor", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.1f, 10.0f });
    addBox("North Wall", { 0.0f, 2.0f, -10.0f }, { 10.0f, 2.5f, 0.1f });
    addBox("South Wall", { 0.0f, 2.0f, 10.0f }, { 10.0f, 2.5f, 0.1f });
    addBox("West Wall", { -10.0f, 2.0f, 0.0f }, { 0.1f, 2.5f, 10.0f });
    addBox("East Wall", { 10.0f, 2.0f, 0.0f }, { 0.1f, 2.5f, 10.0f });

    std::cout << "[scene] seeded starter room: floor, four walls, center block" << std::endl;
}

juce::Vector3D<float> ViewportComponent::SpawnPosition(float distance) const {
    const juce::ScopedLock lock(stateLock_);
    return lastCameraPosition_ + lastCameraForward_ * distance;
}

void ViewportComponent::RunOnGLThread(std::function<void()> work, bool blockUntilFinished) {
    openGLContext_.executeOnGLThread([work = std::move(work)](juce::OpenGLContext&) { work(); }, blockUntilFinished);
}

void ViewportComponent::ResetDemoEntityTransform() {
    const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
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
    if (!vfs_.mount(packagesDir.getChildFile("base.zip"), 0)) {
        std::cout << "[render] failed to mount base.zip; nothing will render." << std::endl;
        return;
    }
    // Mount-priority demo: override.zip only contains a replacement
    // CesiumLogoFlat.png, mounted at higher priority than base.zip.
    const juce::File overrideFile = packagesDir.getChildFile("override.zip");
    if (overrideFile.existsAsFile()) {
        vfs_.mount(overrideFile, 10);
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
    const auto cameraTarget = freeCamera_.Target();
    camera_.SetLookAt(cameraPos, cameraTarget);

    {
        const juce::ScopedLock lock(stateLock_);
        lastCameraPosition_ = cameraPos;
        lastCameraForward_ = cameraTarget - cameraPos; // Target() = Position() + Forward(), already unit length.
    }

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

    // Everything below touches the World's registry — entities, their
    // Transforms, and the Mesh/Material each MeshRenderer points at — so
    // it all runs under one RegistryMutex lock for the rest of this
    // function. entt::registry isn't internally thread-safe, and as of
    // SC3 the message thread (HierarchyPanel, and Stop's
    // ResetDemoEntityTransform) genuinely touches the same registry
    // concurrently with this render loop — this isn't defensive
    // programming against a hypothetical, it's a real, exercised race
    // without this lock.
    const std::lock_guard<std::mutex> registryLock(world_.RegistryMutex());

    // Demo-only: spins the seeded demo entity's Transform based on the
    // World's tick count rather than wall-clock time, so the spin
    // actually stops when the transport is paused/stopped (World::
    // AdvanceTick() is gated on play state in MainComponent's timer) —
    // an editor "playing" a paused simulation should look paused. Only
    // writes when the tick has actually changed since the last frame
    // (not unconditionally every frame): SC4's inspector edits the same
    // Transform from the message thread, and an unconditional per-frame
    // write here would stomp any edit made while paused right back to
    // the spin value on the very next frame.
    const auto currentTick = world_.CurrentTick();
    if (currentTick != lastSpinTick_) {
        lastSpinTick_ = currentTick;
        if (demoEntity_ != entt::null && world_.Registry().valid(demoEntity_) &&
            world_.Registry().all_of<scene::Transform>(demoEntity_)) {
            const float spinRadians = 0.5f * (static_cast<float>(currentTick) / 30.0f);
            world_.Registry().get<scene::Transform>(demoEntity_).eulerRotationRadians.y = spinRadians;
        }
    }

    // Object definitions persist an asset identifier rather than a GPU
    // pointer. Resolve it only once the viewport owns a live GL context.
    auto unresolvedAssets = world_.Registry().view<const scene::MeshAssetReference>();
    for (const auto entity : unresolvedAssets) {
        if (world_.Registry().all_of<scene::MeshRenderer>(entity)) {
            continue;
        }
        const auto asset = assetCatalog_.Find(unresolvedAssets.get<const scene::MeshAssetReference>(entity).assetId);
        if (asset.mesh != nullptr && asset.material != nullptr) {
            world_.Registry().emplace<scene::MeshRenderer>(entity, scene::MeshRenderer{ asset.mesh, asset.material });
        }
    }

    auto drawView = world_.Registry().view<const scene::Transform, const scene::MeshRenderer>();
    for (auto entity : drawView) {
        const auto& renderer = drawView.get<const scene::MeshRenderer>(entity);
        if (!renderer.mesh || !renderer.material) {
            continue;
        }
        if (const auto* sceneFlags = world_.Registry().try_get<const scene::SceneFlags>(entity)) {
            if (!sceneFlags->visible) {
                continue;
            }
        }

        auto* program = renderer.material->Resolve(*shaderComposer_, openGLContext_);
        if (program == nullptr) {
            continue;
        }

        // Transform is local to the entity's parent. Folders are transparent
        // and parent objects establish the coordinate space for descendants.
        const auto model = scene::WorldModelMatrix(world_.Registry(), entity);
        const auto normalMatrix = ExtractUpperLeft3x3(model);

        program->use();
        program->setUniformMat4("uModel", model.mat, 1, GL_FALSE);
        program->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
        program->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
        program->setUniformMat3("uNormalMatrix", normalMatrix.data(), 1, GL_FALSE);
        program->setUniform("uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);

        // Runtime tint overrides leave the shared material unchanged.
        juce::Vector3D<float> tint{ 1.0f, 1.0f, 1.0f };
        if (const auto* runtimeTint = world_.Registry().try_get<const engine::Tint>(entity)) {
            tint = { runtimeTint->color.x, runtimeTint->color.y, runtimeTint->color.z };
        }
        renderer.material->ApplyUniforms(*program, tint);

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

        // AI5: for a skinned entity, advance its Animator (if playing)
        // and upload the resulting bone matrix palette. Reads/writes the
        // Animator component in place -- safe under the same
        // RegistryMutex lock already held for this whole draw pass, and
        // consistent with how the demo-entity spin above mutates a
        // component straight from the render thread.
        if (const auto* skeleton = world_.Registry().try_get<const scene::Skeleton>(entity)) {
            if (!skeleton->joints.empty()) {
                std::vector<juce::Matrix3D<float>> localTransforms;
                auto* animator = world_.Registry().try_get<scene::Animator>(entity);
                const AnimationClip* activeClip = nullptr;
                if (animator != nullptr && animator->clips != nullptr && animator->activeClip >= 0 &&
                    static_cast<std::size_t>(animator->activeClip) < animator->clips->size()) {
                    activeClip = &(*animator->clips)[static_cast<std::size_t>(animator->activeClip)];
                }

                if (activeClip != nullptr) {
                    if (animator->playing) {
                        animator->time += deltaSeconds;
                        if (activeClip->duration > 0.0f) {
                            if (animator->loop) {
                                animator->time = std::fmod(animator->time, activeClip->duration);
                                if (animator->time < 0.0f) {
                                    animator->time += activeClip->duration;
                                }
                            } else if (animator->time > activeClip->duration) {
                                animator->time = activeClip->duration;
                                animator->playing = false;
                            }
                        }
                    }
                    localTransforms = scene::SampleLocalTransforms(*activeClip, animator->time, *skeleton);
                } else {
                    localTransforms.reserve(skeleton->joints.size());
                    for (const auto& joint : skeleton->joints) {
                        localTransforms.push_back(joint.localBindTransform);
                    }
                }

                const auto skinningMatrices = scene::ComputeSkinningMatrices(*skeleton, localTransforms);
                const int boneCount = juce::jmin(static_cast<int>(skinningMatrices.size()), kMaxBones);
                for (int b = 0; b < boneCount; ++b) {
                    const juce::String uniformName = "uBoneMatrices[" + juce::String(b) + "]";
                    program->setUniformMat4(uniformName.toRawUTF8(), skinningMatrices[static_cast<std::size_t>(b)].mat,
                                             1, GL_FALSE);
                }
            }
        }

        renderer.mesh->Draw();
    }

    LogGLErrors("draw");
}

void ViewportComponent::openGLContextClosing() {
    {
        const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto view = world_.Registry().view<scene::MeshRenderer>();
        for (auto entity : view) {
            if (auto& material = view.get<scene::MeshRenderer>(entity).material) {
                material->InvalidateCache();
            }
        }
    }
    gridProgram_ = nullptr;
    shaderComposer_.reset();
}

void ViewportComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    freeCamera_.AdjustSpeed(wheel.deltaY);
}

} // namespace ce
