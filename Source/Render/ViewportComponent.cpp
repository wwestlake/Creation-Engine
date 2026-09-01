#include "Render/ViewportComponent.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>

#include "engine/core_components.h"
#include "engine/gameplay_components.h"
#include "Assets/EngineAssetPack.h"
#include "Assets/ProjectContentAssetStore.h"
#include "Render/Import/GltfLoader.h"
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

juce::Vector3D<float> RotateBy(const ce::engine::vr::Quaternion& q, const juce::Vector3D<float>& v) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return { (1.0f - 2.0f * (yy + zz)) * v.x + 2.0f * (xy - wz) * v.y + 2.0f * (xz + wy) * v.z,
             2.0f * (xy + wz) * v.x + (1.0f - 2.0f * (xx + zz)) * v.y + 2.0f * (yz - wx) * v.z,
             2.0f * (xz - wy) * v.x + 2.0f * (yz + wx) * v.y + (1.0f - 2.0f * (xx + yy)) * v.z };
}

juce::Vector3D<float> RotateYaw(const juce::Vector3D<float>& value, float yaw) {
    const float cosine = std::cos(yaw), sine = std::sin(yaw);
    return { value.x * cosine + value.z * sine, value.y, -value.x * sine + value.z * cosine };
}

juce::Vector3D<float> RotateAround(const juce::Vector3D<float>& value, const juce::Vector3D<float>& axis, float radians) {
    const auto unitAxis = axis.normalised();
    const float cosine = std::cos(radians), sine = std::sin(radians);
    return value * cosine + (unitAxis ^ value) * sine + unitAxis * (unitAxis * value) * (1.0f - cosine);
}

float RaySegmentDistance(const juce::Vector3D<float>& rayOrigin, const juce::Vector3D<float>& rayDirection,
                         const juce::Vector3D<float>& segmentStart, const juce::Vector3D<float>& segmentEnd,
                         float& rayDistance)
{
    const auto segment = segmentEnd - segmentStart;
    const auto originOffset = rayOrigin - segmentStart;
    const float a = rayDirection * rayDirection;
    const float b = rayDirection * segment;
    const float c = segment * segment;
    const float d = rayDirection * originOffset;
    const float e = segment * originOffset;
    const float denominator = a * c - b * b;
    float t = 0.0f;
    float s = c > 0.00001f ? juce::jlimit(0.0f, 1.0f, e / c) : 0.0f;
    if (std::abs(denominator) > 0.00001f) {
        t = (b * e - c * d) / denominator;
        s = juce::jlimit(0.0f, 1.0f, (a * e - b * d) / denominator);
        t = juce::jmax(0.0f, (b * s - d) / a);
    }
    rayDistance = t;
    return (rayOrigin + rayDirection * t - (segmentStart + segment * s)).length();
}

bool RayAxisAlignedBoxHit(const juce::Vector3D<float>& rayOrigin, const juce::Vector3D<float>& rayDirection,
                          const juce::Vector3D<float>& center, const juce::Vector3D<float>& halfExtents,
                          float& distance, juce::Vector3D<float>& surfaceNormal)
{
    const float origin[3]{ rayOrigin.x, rayOrigin.y, rayOrigin.z };
    const float direction[3]{ rayDirection.x, rayDirection.y, rayDirection.z };
    const float minimum[3]{ center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.z };
    const float maximum[3]{ center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.z };
    float enter = 0.0f, exit = 8.0f;
    juce::Vector3D<float> enterNormal{};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 0.00001f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
            continue;
        }
        float nearT = (minimum[axis] - origin[axis]) / direction[axis];
        float farT = (maximum[axis] - origin[axis]) / direction[axis];
        const float sign = nearT <= farT ? -1.0f : 1.0f;
        if (nearT > farT) std::swap(nearT, farT);
        if (nearT > enter) {
            enter = nearT;
            enterNormal = {};
            if (axis == 0) enterNormal.x = sign;
            if (axis == 1) enterNormal.y = sign;
            if (axis == 2) enterNormal.z = sign;
        }
        exit = juce::jmin(exit, farT);
        if (enter > exit) return false;
    }
    if (exit <= 0.0f) return false;
    distance = enter > 0.0f ? enter : exit;
    surfaceNormal = enter > 0.0f ? enterNormal : -rayDirection;
    return distance <= 8.0f;
}

// The OpenXR pose is room-scale movement relative to the headset's tracking
// origin.  It is not an editor-world position by itself: placing it directly
// at {0, 0, 0} spawned the user inside the starter scene's center block.  Keep
// a separate editor rig origin, then add real-world head movement on top of
// it.  This is the foundation for the seated flying cart and the later
// standing/walking mode; those modes will move the rig, never reinterpret the
// physical tracking pose.
const juce::Vector3D<float> kInitialVREditorRigPosition{ 0.0f, 0.0f, 5.0f };

void ConfigureVRCamera(ce::Camera& camera, const ce::engine::vr::View& eye,
                       const juce::Vector3D<float>& rigPosition, float rigYaw, float rigPitch) {
    constexpr float nearPlane = 0.1f;
    constexpr float farPlane = 100.0f;
    const auto& frustum = eye.frustumTangents;
    camera.SetFrustum(frustum[0] * nearPlane, frustum[1] * nearPlane,
                      frustum[2] * nearPlane, frustum[3] * nearPlane, nearPlane, farPlane);
    const juce::Vector3D<float> position = rigPosition + RotateYaw({ eye.pose.position.x, eye.pose.position.y, eye.pose.position.z }, rigYaw);
    auto forward = RotateYaw(RotateBy(eye.pose.orientation, { 0.0f, 0.0f, -1.0f }), rigYaw);
    auto up = RotateYaw(RotateBy(eye.pose.orientation, { 0.0f, 1.0f, 0.0f }), rigYaw);
    const auto right = (forward ^ up).normalised();
    forward = RotateAround(forward, right, rigPitch);
    up = (right ^ forward).normalised();
    camera.SetLookAt(position, position + forward, up);
}

} // namespace

namespace ce {

ViewportComponent::ViewportComponent(engine::World& world, interaction::EditorInteraction& interactions)
    : world_(world), interactions_(interactions), freeCamera_(*this) {
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

    // A project scene may already have been restored before this OpenGL
    // context becomes ready. The standard room is a new-project template,
    // not content to inject into every authored scene. Checked via
    // SceneFlags rather than Name: EngineSceneSerializer::restoreScene
    // only attaches Name when the saved entity actually had one (e.g. an
    // unnamed FBX-imported mesh won't), so a Name-based check can read a
    // just-restored scene as empty and wrongly reseed the demo room on
    // top of it. SceneFlags is unconditionally attached to every entity,
    // by both restoreScene and this function's own addMesh lambda below.
    {
        const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        if (!world_.Registry().view<const scene::SceneFlags>().empty()) return;
    }

    // The starter scene is intentionally built only from the procedural
    // primitive catalog. Store/bundled content can enrich it later, but the
    // first playable room must work in a fresh project with no downloads.
    const auto cube = assetCatalog_.Find("Cube");
    const auto sphere = assetCatalog_.Find("Sphere");
    if (cube.mesh == nullptr || sphere.mesh == nullptr) {
        std::cout << "[scene] no cube asset in catalog; starter room will be empty." << std::endl;
        return;
    }

    const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto addMesh = [&](const char* name, const scene::AssetCatalog::Asset& meshAsset,
                       engine::Vec3 position, engine::Vec3 scale) {
        const auto entity = world_.CreateEntity();
        scene::Transform transform;
        transform.position = position;
        transform.scale = scale;
        world_.Registry().emplace<scene::Name>(entity, scene::Name{ name });
        world_.Registry().emplace<scene::Transform>(entity, transform);
        world_.Registry().emplace<scene::MeshRenderer>(entity, scene::MeshRenderer{ meshAsset.mesh, meshAsset.material });
        world_.Registry().emplace<scene::SceneFlags>(entity, scene::SceneFlags{});
        return entity;
    };

    demoEntity_ = addMesh("Center Block", cube, { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f });
    addMesh("Floor", cube, { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.1f, 10.0f });
    addMesh("North Wall", cube, { 0.0f, 2.0f, -10.0f }, { 10.0f, 2.5f, 0.1f });
    addMesh("South Wall", cube, { 0.0f, 2.0f, 10.0f }, { 10.0f, 2.5f, 0.1f });
    addMesh("West Wall", cube, { -10.0f, 2.0f, 0.0f }, { 0.1f, 2.5f, 10.0f });
    addMesh("East Wall", cube, { 10.0f, 2.0f, 0.0f }, { 0.1f, 2.5f, 10.0f });
    addMesh("Box A", cube, { -3.0f, 0.75f, -2.0f }, { 1.5f, 1.5f, 1.5f });
    addMesh("Box B", cube, { 3.0f, 0.5f, -1.0f }, { 1.0f, 1.0f, 1.0f });

    for (int index = 0; index < 4; ++index)
    {
        const auto ball = addMesh((std::string("Physics Ball ") + std::to_string(index + 1)).c_str(),
                                  sphere, { -2.0f + static_cast<float>(index) * 1.35f,
                                            3.0f + static_cast<float>(index) * 0.7f, 1.5f },
                                  { 0.5f, 0.5f, 0.5f });
        world_.Registry().emplace<engine::RigidBody>(ball, engine::RigidBody{ {}, 0.5f, -9.8f, 0.72f, true });
    }

    std::cout << "[scene] seeded starter room: floor, walls, boxes, glass placeholders, physics balls" << std::endl;
}

juce::Vector3D<float> ViewportComponent::SpawnPosition(float distance) const {
    const juce::ScopedLock lock(stateLock_);
    return lastCameraPosition_ + lastCameraForward_ * distance;
}

void ViewportComponent::ResetDemoEntityTransform() {
    const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    if (demoEntity_ == entt::null || !world_.Registry().valid(demoEntity_)) {
        return;
    }
    world_.Registry().get<scene::Transform>(demoEntity_) = scene::Transform{};
}

void ViewportComponent::ResolveProjectAssets(const creation::assets::ProjectSession& session,
                                             const creation::suite::SuiteSettings& settings)
{
    struct PendingAsset { juce::String id; juce::String versionId; };
    struct PendingPack { juce::String id; juce::String version; };
    std::vector<PendingAsset> pending;
    std::vector<PendingPack> packs;
    {
        const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        const auto references = world_.Registry().view<const scene::MeshAssetReference>();
        for (const auto entity : references)
        {
            const auto& reference = references.get<const scene::MeshAssetReference>(entity);
            if (reference.packId.isNotEmpty() && reference.packVersion.isNotEmpty())
            {
                const auto packExists = std::find_if(packs.begin(), packs.end(), [&reference](const PendingPack& item) {
                    return item.id == reference.packId && item.version == reference.packVersion;
                });
                if (packExists == packs.end()) packs.push_back({ reference.packId, reference.packVersion });
            }
            if (reference.packId.isNotEmpty() || reference.versionId.isEmpty() || assetCatalog_.Find(reference.assetId).mesh != nullptr) continue;
            const auto exists = std::find_if(pending.begin(), pending.end(), [&reference](const PendingAsset& item) {
                return item.id == reference.assetId && item.versionId == reference.versionId;
            });
            if (exists == pending.end()) pending.push_back({ reference.assetId, reference.versionId });
        }
    }

    for (const auto& pack : packs)
    {
        juce::String error;
        RunOnGLThread([this, &pack, &error] {
            assetCatalog_.LoadAssetPack(pack.id, pack.version, error);
        }, true);
        if (error.isNotEmpty())
            juce::Logger::writeToLog("Creation Engine pack resolver: " + pack.id + " " + pack.version + ": " + error);
    }

    for (const auto& item : pending)
    {
        LoadedModel model;
        creation::assets::MaterializedAssetLease lease;
        juce::String error;
        if (! assets::ProjectContentAssetStore::loadRenderable(session, settings, item.id, item.versionId, model, lease, error))
        {
            juce::Logger::writeToLog("Creation Engine asset resolver: " + item.id + ": " + error);
            continue;
        }
        RunOnGLThread([this, &item, &model] { assetCatalog_.AddFromModel(item.id, model); }, true);
        juce::String releaseError;
        creation::assets::AssetMaterializer::releaseLease(lease, releaseError);
    }
}

bool ViewportComponent::desktopRay(const juce::Point<float>& screenPosition,
                                   juce::Vector3D<float>& origin,
                                   juce::Vector3D<float>& direction) const
{
    if (getWidth() <= 0 || getHeight() <= 0) return false;
    juce::Vector3D<float> forward;
    {
        const juce::ScopedLock lock(stateLock_);
        origin = lastCameraPosition_;
        forward = lastCameraForward_.normalised();
    }
    if (forward.length() < 0.001f) return false;
    const auto worldUp = juce::Vector3D<float>{ 0.0f, 1.0f, 0.0f };
    const auto right = (forward ^ worldUp).normalised();
    const auto up = (right ^ forward).normalised();
    const float x = 2.0f * screenPosition.x / static_cast<float>(getWidth()) - 1.0f;
    const float y = 1.0f - 2.0f * screenPosition.y / static_cast<float>(getHeight());
    constexpr float verticalFov = juce::MathConstants<float>::pi / 4.0f;
    const float tangent = std::tan(verticalFov * 0.5f);
    const float aspect = static_cast<float>(getWidth()) / static_cast<float>(getHeight());
    direction = (forward + right * (x * aspect * tangent) + up * (y * tangent)).normalised();
    return true;
}

entt::entity ViewportComponent::desktopPick(const juce::Vector3D<float>& origin,
                                            const juce::Vector3D<float>& direction,
                                            float& distance) const
{
    distance = 8.0f;
    entt::entity closest = entt::null;
    const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    const auto view = world_.Registry().view<const scene::Transform, const scene::MeshAssetReference>();
    for (const auto entity : view) {
        const auto& transform = view.get<const scene::Transform>(entity);
        if (const auto* flags = world_.Registry().try_get<const scene::SceneFlags>(entity); flags != nullptr && !flags->visible)
            continue;
        const auto halfExtents = juce::Vector3D<float>{ std::abs(transform.scale.x) * 0.5f,
                                                          std::abs(transform.scale.y) * 0.5f,
                                                          std::abs(transform.scale.z) * 0.5f };
        float hitDistance = 0.0f;
        juce::Vector3D<float> normal;
        if (RayAxisAlignedBoxHit(origin, direction, scene::ToJuceVector3D(transform.position), halfExtents,
                                 hitDistance, normal) && hitDistance < distance) {
            distance = hitDistance;
            closest = entity;
        }
    }
    return closest;
}

interaction::TranslationConstraint ViewportComponent::constraintFor(TranslationHandle handle) const
{
    switch (handle) {
        case TranslationHandle::xAxis: return { true, false, false };
        case TranslationHandle::yAxis: return { false, true, false };
        case TranslationHandle::zAxis: return { false, false, true };
        case TranslationHandle::xyPlane: return { true, true, false };
        case TranslationHandle::xzPlane: return { true, false, true };
        case TranslationHandle::yzPlane: return { false, true, true };
        default: return {};
    }
}

ViewportComponent::TranslationHandle ViewportComponent::desktopGizmoHandle(
    const juce::Vector3D<float>& origin, const juce::Vector3D<float>& direction) const
{
    if (vrTransformGizmo_.entity == entt::null) return TranslationHandle::none;
    const auto& center = vrTransformGizmo_.center;
    const float size = vrTransformGizmo_.scale;
    const auto toCenter = center - origin;
    const float centerDistance = toCenter * direction;
    if (centerDistance > 0.0f && (toCenter - direction * centerDistance).length() < size * 0.13f)
        return TranslationHandle::free;
    const std::array<std::pair<TranslationHandle, juce::Vector3D<float>>, 3> axes{{
        { TranslationHandle::xAxis, { 1.0f, 0.0f, 0.0f } },
        { TranslationHandle::yAxis, { 0.0f, 1.0f, 0.0f } },
        { TranslationHandle::zAxis, { 0.0f, 0.0f, 1.0f } }
    }};
    for (const auto& [handle, axis] : axes) {
        float rayDistance = 0.0f;
        if (RaySegmentDistance(origin, direction, center + axis * (size * 0.18f), center + axis * size, rayDistance) < size * 0.12f)
            return handle;
    }
    return TranslationHandle::none;
}

void ViewportComponent::updateDesktopTransformGizmo()
{
    vrHoverActive_ = false;
    vrTransformGizmo_ = {};
    const auto selected = interactions_.selected();
    if (selected == entt::null) return;
    const std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(selected) || !registry.all_of<scene::Transform>(selected)) return;
    vrTransformGizmo_.entity = selected;
    vrTransformGizmo_.center = scene::ToJuceVector3D(registry.get<scene::Transform>(selected).position);
    vrTransformGizmo_.scale = 0.8f;
}

void ViewportComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || event.mods.isRightButtonDown()) return;
    grabKeyboardFocus();
    juce::Vector3D<float> origin, direction;
    if (!desktopRay(event.position, origin, direction)) return;
    const auto handle = desktopGizmoHandle(origin, direction);
    if (handle != TranslationHandle::none && vrTransformGizmo_.entity != entt::null) {
        desktopDragDistance_ = juce::jmax(0.2f, (vrTransformGizmo_.center - origin).length());
        if (interactions_.beginTranslation(vrTransformGizmo_.entity, scene::ToVec3(origin + direction * desktopDragDistance_), constraintFor(handle)))
            desktopTransformDrag_ = true;
        return;
    }
    float hitDistance = 0.0f;
    interactions_.select(desktopPick(origin, direction, hitDistance));
}

void ViewportComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!desktopTransformDrag_) return;
    juce::Vector3D<float> origin, direction;
    if (desktopRay(event.position, origin, direction))
        interactions_.updateTranslation(scene::ToVec3(origin + direction * desktopDragDistance_));
}

void ViewportComponent::mouseUp(const juce::MouseEvent&)
{
    if (!desktopTransformDrag_) return;
    desktopTransformDrag_ = false;
    interactions_.endGrab();
}

void ViewportComponent::RunOnGLThread(std::function<void()> work, bool blockUntilFinished) {
    openGLContext_.executeOnGLThread([work = std::move(work)](juce::OpenGLContext&) { work(); }, blockUntilFinished);
}

void ViewportComponent::updateVREditorRig(float deltaSeconds)
{
    if (interactions_.locomotionMode() != interaction::LocomotionMode::seatedFly) return;
    const auto deadZone = [](float value) { return std::abs(value) > 0.15f ? value : 0.0f; };
    const auto& move = vrFrame_.leftController.thumbstick;
    const auto& steer = vrFrame_.rightController.thumbstick;
    vrRigYaw_ += deadZone(steer.x) * 1.7f * deltaSeconds;
    // Pushing the right stick forward looks down; pulling it back looks up.
    vrRigPitch_ = juce::jlimit(-1.15f, 1.15f, vrRigPitch_ - deadZone(steer.y) * 1.2f * deltaSeconds);
    // Fly in the direction the headset camera is looking, including pitch.
    // Strafe remains horizontal so left/right stays predictable while seated.
    auto forward = RotateYaw(RotateBy(vrFrame_.leftEye.pose.orientation, { 0.0f, 0.0f, -1.0f }), vrRigYaw_);
    const auto cameraUp = RotateYaw(RotateBy(vrFrame_.leftEye.pose.orientation, { 0.0f, 1.0f, 0.0f }), vrRigYaw_);
    const auto cameraRight = (forward ^ cameraUp).normalised();
    forward = RotateAround(forward, cameraRight, vrRigPitch_).normalised();
    const auto right = juce::Vector3D<float>{ std::cos(vrRigYaw_), 0.0f, std::sin(vrRigYaw_) };
    constexpr float speed = 4.0f;
    vrRigPosition_ += forward * (deadZone(move.y) * speed * deltaSeconds);
    vrRigPosition_ += right * (deadZone(move.x) * speed * deltaSeconds);
    if (vrFrame_.rightController.primaryPressed) vrRigPosition_.y += speed * deltaSeconds;
    if (vrFrame_.leftController.primaryPressed) vrRigPosition_.y -= speed * deltaSeconds;
}

void ViewportComponent::updateVRInteraction()
{
    vrHoverActive_ = false;
    vrTransformGizmo_ = {};
    const auto selected = interactions_.selected();
    if (selected != entt::null) {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(selected) && registry.all_of<scene::Transform>(selected)) {
            vrTransformGizmo_.entity = selected;
            vrTransformGizmo_.center = scene::ToJuceVector3D(registry.get<scene::Transform>(selected).position);
            vrTransformGizmo_.scale = 0.8f;
        }
    }

    const auto constraintFor = [](TranslationHandle handle) {
        switch (handle) {
            case TranslationHandle::xAxis: return interaction::TranslationConstraint{ true, false, false };
            case TranslationHandle::yAxis: return interaction::TranslationConstraint{ false, true, false };
            case TranslationHandle::zAxis: return interaction::TranslationConstraint{ false, false, true };
            case TranslationHandle::xyPlane: return interaction::TranslationConstraint{ true, true, false };
            case TranslationHandle::xzPlane: return interaction::TranslationConstraint{ true, false, true };
            case TranslationHandle::yzPlane: return interaction::TranslationConstraint{ false, true, true };
            default: return interaction::TranslationConstraint{};
        }
    };
    const auto findHandle = [&](const VRWandRay& wand) {
        if (vrTransformGizmo_.entity == entt::null) return TranslationHandle::none;
        const auto& c = vrTransformGizmo_.center;
        const float length = vrTransformGizmo_.scale;
        const std::array<std::pair<TranslationHandle, juce::Vector3D<float>>, 3> axes{{
            { TranslationHandle::xAxis, { 1.0f, 0.0f, 0.0f } },
            { TranslationHandle::yAxis, { 0.0f, 1.0f, 0.0f } },
            { TranslationHandle::zAxis, { 0.0f, 0.0f, 1.0f } }
        }};
        TranslationHandle result = TranslationHandle::none;
        float nearest = 8.1f;
        const auto rayDirection = (wand.end - wand.start).normalised();
        const auto toCenter = c - wand.start;
        const float centerDistance = toCenter * rayDirection;
        if (centerDistance > 0.0f && centerDistance < nearest &&
            (toCenter - rayDirection * centerDistance).length() < 0.12f * length) {
            nearest = centerDistance;
            result = TranslationHandle::free;
        }
        for (const auto& [handle, axis] : axes) {
            float rayDistance = 0.0f;
            if (RaySegmentDistance(wand.start, rayDirection, c, c + axis * length, rayDistance) < 0.10f &&
                rayDistance < nearest) {
                nearest = rayDistance;
                result = handle;
            }
        }
        const auto planeHandle = [&](TranslationHandle handle, int a, int b) {
            const auto direction = rayDirection;
            const float directionComponent[3]{ direction.x, direction.y, direction.z };
            const float originComponent[3]{ wand.start.x, wand.start.y, wand.start.z };
            const float centerComponent[3]{ c.x, c.y, c.z };
            const int normal = 3 - a - b;
            if (std::abs(directionComponent[normal]) < 0.0001f) return;
            const float distance = (centerComponent[normal] - originComponent[normal]) / directionComponent[normal];
            if (distance <= 0.0f || distance >= nearest) return;
            const auto hit = wand.start + direction * distance;
            const float hitComponent[3]{ hit.x, hit.y, hit.z };
            const float low = 0.16f * length, high = 0.48f * length;
            const float da = std::abs(hitComponent[a] - centerComponent[a]);
            const float db = std::abs(hitComponent[b] - centerComponent[b]);
            if (da >= low && da <= high && db >= low && db <= high) { nearest = distance; result = handle; }
        };
        planeHandle(TranslationHandle::xyPlane, 0, 1);
        planeHandle(TranslationHandle::xzPlane, 0, 2);
        planeHandle(TranslationHandle::yzPlane, 1, 2);
        return result;
    };

    const std::array<engine::vr::ControllerState, 2> controllers{ vrFrame_.leftController, vrFrame_.rightController };
    for (std::size_t index = 0; index < controllers.size(); ++index) {
        const auto& controller = controllers[index];
        auto& wand = vrWands_[index];
        wand = {};
        if (!controller.connected) continue;
        wand.active = true;
        const auto origin = vrRigPosition_ + RotateYaw({ controller.pose.position.x, controller.pose.position.y, controller.pose.position.z }, vrRigYaw_);
        const auto direction = RotateYaw(RotateBy(controller.pose.orientation, { 0.0f, 0.0f, -1.0f }), vrRigYaw_).normalised();
        wand.start = origin + direction * 0.08f;
        wand.end = origin + direction * 8.0f;
        float nearest = 8.0f;
        {
            std::lock_guard<std::mutex> lock(world_.RegistryMutex());
            auto& registry = world_.Registry();
            const auto entities = registry.view<const scene::Transform, const scene::MeshRenderer>();
            for (const auto entity : entities) {
                const auto& transform = entities.get<const scene::Transform>(entity);
                const auto center = scene::ToJuceVector3D(transform.position);
                const juce::Vector3D<float> halfExtents{
                    std::abs(transform.scale.x) * 0.5f,
                    std::abs(transform.scale.y) * 0.5f,
                    std::abs(transform.scale.z) * 0.5f
                };
                float hitDistance = 0.0f;
                juce::Vector3D<float> hitNormal;
                if (RayAxisAlignedBoxHit(wand.start, direction, center, halfExtents, hitDistance, hitNormal) && hitDistance < nearest) {
                    nearest = hitDistance;
                    wand.hit = true;
                    wand.entity = entity;
                    wand.surfaceNormal = hitNormal;
                }
            }
        }
        if (wand.hit) wand.end = wand.start + direction * nearest;
        if (wand.hit && !vrHoverActive_) {
            // Mark the exact wand contact point.  Scaling feedback by the
            // selected mesh made a wall produce an absurd room-sized ring.
            vrHoverCenter_ = wand.end;
            vrHoverNormal_ = wand.surfaceNormal;
            vrHoverRadius_ = 0.16f;
            vrHoverActive_ = true;
        }
        vrHoveredHandles_[index] = findHandle(wand);
        if (vrHoveredHandles_[index] != TranslationHandle::none) vrTransformGizmo_.hovered = vrHoveredHandles_[index];
        if (controller.selectPressed && !vrSelectWasPressed_[index] && wand.hit) interactions_.select(wand.entity);
        if (controller.gripPressed && !vrGripWasPressed_[index]) {
            if (vrHoveredHandles_[index] != TranslationHandle::none && vrTransformGizmo_.entity != entt::null) {
                if (interactions_.beginTranslation(vrTransformGizmo_.entity, scene::ToVec3(origin), constraintFor(vrHoveredHandles_[index])))
                    vrTranslationController_ = static_cast<int>(index);
            } else if (wand.hit && interactions_.beginTranslation(wand.entity, scene::ToVec3(origin), {})) {
                vrTranslationController_ = static_cast<int>(index);
            }
        }
        if (controller.gripPressed && vrTranslationController_ == static_cast<int>(index))
            interactions_.updateTranslation(scene::ToVec3(origin));
        if (!controller.gripPressed && vrGripWasPressed_[index] && vrTranslationController_ == static_cast<int>(index)) {
            interactions_.endGrab();
            vrTranslationController_ = -1;
        }
        vrSelectWasPressed_[index] = controller.selectPressed;
        vrGripWasPressed_[index] = controller.gripPressed;
    }
}

void ViewportComponent::uploadVRWands()
{
    struct WandVertex { float position[3]; };
    std::vector<WandVertex> vertices;
    for (const auto& wand : vrWands_) {
        const auto direction = wand.active ? (wand.end - wand.start).normalised() : juce::Vector3D<float>{};
        // This is deliberately a distinct, substantial handle rather than
        // merely the first few centimetres of the ray.  It keeps the editor
        // tool readable as a wand, even against the bright room/grid.
        const auto handleEnd = wand.start + direction * 0.42f;
        for (const auto& point : { wand.start, handleEnd, handleEnd, wand.end })
            vertices.push_back({ { point.x, point.y, point.z } });
    }
    vrWandVertexCount_ = static_cast<GLsizei>(vertices.size());
    if (vertices.empty()) return;
    vrWandVertexBuffer_.Upload(GL_ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(WandVertex), GL_DYNAMIC_DRAW);
    vrWandVertexArray_.Bind();
    vrWandVertexBuffer_.Bind(GL_ARRAY_BUFFER);
    vrWandVertexArray_.SetAttribute(0, 3, sizeof(WandVertex), 0);
    gl::VertexArray::Unbind();
}

void ViewportComponent::uploadVRTransformGizmo()
{
    struct GizmoVertex { float position[3]; };
    // Three arrow groups (6 vertices each), plane squares (24), and a
    // central free-move cube (24).  Keep these fixed offsets so the draw
    // path can colour the conventional editor handles independently.
    std::vector<GizmoVertex> vertices(66);
    const auto writeLine = [&](std::size_t& cursor, const juce::Vector3D<float>& a, const juce::Vector3D<float>& b) {
        vertices[cursor++] = { { a.x, a.y, a.z } };
        vertices[cursor++] = { { b.x, b.y, b.z } };
    };
    const auto addLine = [&](const juce::Vector3D<float>& a, const juce::Vector3D<float>& b) {
        vertices.push_back({ { a.x, a.y, a.z } });
        vertices.push_back({ { b.x, b.y, b.z } });
    };
    if (vrTransformGizmo_.entity != entt::null) {
        const auto c = vrTransformGizmo_.center;
        const float s = vrTransformGizmo_.scale;
        const std::array<juce::Vector3D<float>, 3> axes{{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }};
        for (std::size_t axis = 0; axis < axes.size(); ++axis) {
            const auto end = c + axes[axis] * s;
            const auto sideA = axis == 0 ? axes[1] : axes[0];
            const auto sideB = axis == 2 ? axes[1] : axes[2];
            std::size_t cursor = axis * 6;
            writeLine(cursor, c, end);
            writeLine(cursor, end, end - axes[axis] * (0.18f * s) + sideA * (0.10f * s));
            writeLine(cursor, end, end - axes[axis] * (0.18f * s) + sideB * (0.10f * s));
        }
        std::size_t planeCursor = 18;
        const auto addPlane = [&](const juce::Vector3D<float>& a, const juce::Vector3D<float>& b) {
            const auto p0 = c + (a + b) * (0.18f * s);
            const auto p1 = c + (a * 0.50f + b * 0.18f) * s;
            const auto p2 = c + (a + b) * (0.50f * s);
            const auto p3 = c + (a * 0.18f + b * 0.50f) * s;
            for (const auto& pair : std::array<std::pair<juce::Vector3D<float>, juce::Vector3D<float>>, 4>{{ { p0, p1 }, { p1, p2 }, { p2, p3 }, { p3, p0 } }})
                writeLine(planeCursor, pair.first, pair.second);
        };
        addPlane(axes[0], axes[1]); // XY
        addPlane(axes[0], axes[2]); // XZ
        addPlane(axes[1], axes[2]); // YZ
        const float half = 0.11f * s;
        const std::array<juce::Vector3D<float>, 8> cube{{
            c + juce::Vector3D<float>{ -half, -half, -half }, c + juce::Vector3D<float>{ half, -half, -half },
            c + juce::Vector3D<float>{ half, half, -half }, c + juce::Vector3D<float>{ -half, half, -half },
            c + juce::Vector3D<float>{ -half, -half, half }, c + juce::Vector3D<float>{ half, -half, half },
            c + juce::Vector3D<float>{ half, half, half }, c + juce::Vector3D<float>{ -half, half, half }
        }};
        std::size_t cubeCursor = 42;
        for (const auto& edge : std::array<std::pair<int, int>, 12>{{ { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } }})
            writeLine(cubeCursor, cube[edge.first], cube[edge.second]);
    }
    if (vrHoverActive_) {
        constexpr int segments = 24;
        const auto helper = std::abs(vrHoverNormal_.y) < 0.9f ? juce::Vector3D<float>{ 0.0f, 1.0f, 0.0f } : juce::Vector3D<float>{ 1.0f, 0.0f, 0.0f };
        const auto ringU = (vrHoverNormal_ ^ helper).normalised();
        const auto ringV = (vrHoverNormal_ ^ ringU).normalised();
        for (int segment = 0; segment < segments; ++segment) {
            const float a = juce::MathConstants<float>::twoPi * static_cast<float>(segment) / segments;
            const float b = juce::MathConstants<float>::twoPi * static_cast<float>(segment + 1) / segments;
            addLine(vrHoverCenter_ + ringU * (std::cos(a) * vrHoverRadius_) + ringV * (std::sin(a) * vrHoverRadius_),
                    vrHoverCenter_ + ringU * (std::cos(b) * vrHoverRadius_) + ringV * (std::sin(b) * vrHoverRadius_));
        }
    }
    vrGizmoVertexCount_ = static_cast<GLsizei>(vertices.size());
    vrGizmoVertexBuffer_.Upload(GL_ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(GizmoVertex), GL_DYNAMIC_DRAW);
    vrGizmoVertexArray_.Bind();
    vrGizmoVertexBuffer_.Bind(GL_ARRAY_BUFFER);
    vrGizmoVertexArray_.SetAttribute(0, 3, sizeof(GizmoVertex), 0);
    gl::VertexArray::Unbind();
}

void ViewportComponent::uploadVREditorCart()
{
    struct CartVertex { float position[3]; };
    std::vector<CartVertex> vertices;
    const auto addLine = [&](const juce::Vector3D<float>& a, const juce::Vector3D<float>& b) {
        vertices.push_back({ { a.x, a.y, a.z } });
        vertices.push_back({ { b.x, b.y, b.z } });
    };
    // The cart is authored in world space beneath the tracked player root;
    // it travels with the flying rig and is not a static room prop.
    const float deckY = vrRigPosition_.y - 0.18f;
    const float left = vrRigPosition_.x - 1.25f, right = vrRigPosition_.x + 1.25f;
    const float front = vrRigPosition_.z - 1.10f, back = vrRigPosition_.z + 1.10f;
    const juce::Vector3D<float> fl{ left, deckY, front }, fr{ right, deckY, front };
    const juce::Vector3D<float> bl{ left, deckY, back }, br{ right, deckY, back };
    // Opaque top deck. The rails below are only the cart's outline.
    for (const auto& point : std::array<juce::Vector3D<float>, 6>{{ fl, br, fr, fl, bl, br }})
        vertices.push_back({ { point.x, point.y, point.z } });
    vrCartDeckVertexCount_ = static_cast<GLsizei>(vertices.size());
    addLine(fl, fr); addLine(fr, br); addLine(br, bl); addLine(bl, fl);
    // Low open rails establish a stable frame of reference without turning
    // the cart into a box around the user.
    const float railY = deckY + 0.26f;
    addLine(fl, { left, railY, front }); addLine(fr, { right, railY, front });
    addLine({ left, railY, front }, { right, railY, front });
    addLine(bl, { left, railY, back }); addLine(br, { right, railY, back });
    // Simple seat back: an explicit visual cue that the seated mode belongs
    // in a vehicle, not as an invisible flying camera.
    const float seatY = deckY + 0.72f;
    addLine({ -0.72f + vrRigPosition_.x, deckY, back - 0.22f }, { -0.72f + vrRigPosition_.x, seatY, back - 0.22f });
    addLine({ 0.72f + vrRigPosition_.x, deckY, back - 0.22f }, { 0.72f + vrRigPosition_.x, seatY, back - 0.22f });
    addLine({ -0.72f + vrRigPosition_.x, seatY, back - 0.22f }, { 0.72f + vrRigPosition_.x, seatY, back - 0.22f });
    vrCartVertexCount_ = static_cast<GLsizei>(vertices.size());
    vrCartVertexBuffer_.Upload(GL_ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(CartVertex), GL_DYNAMIC_DRAW);
    vrCartVertexArray_.Bind();
    vrCartVertexBuffer_.Bind(GL_ARRAY_BUFFER);
    vrCartVertexArray_.SetAttribute(0, 3, sizeof(CartVertex), 0);
    gl::VertexArray::Unbind();
}

void ViewportComponent::newOpenGLContextCreated() {
    std::cout << "[render] newOpenGLContextCreated: GL_VERSION="
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;

    // OpenXR is created only after JUCE has made this OpenGL context current.
    // It remains optional so the editor still starts normally without a
    // headset or an installed OpenXR runtime.
    openXRProvider_ = std::make_unique<vr::OpenXRProvider>();
    if (!openXRProvider_->initialize() ||
        !openXRProvider_->initializeOpenGL(openGLContext_.getRawContext())) {
        std::cout << "[vr] OpenXR unavailable; continuing in desktop mode." << std::endl;
        openXRProvider_->shutdown();
        openXRProvider_.reset();
    } else {
        std::cout << "[vr] OpenXR graphics session initialized." << std::endl;
    }

    shaderComposer_ = std::make_unique<ShaderComposer>(juce::File(CE_SHADER_SOURCE_DIR));

    // Proves the composer handles more than one program/variant: the
    // unlit debug program is compiled and cached here even though the
    // demo scene below only draws through PBR materials.
    shaderComposer_->GetProgram(openGLContext_, "programs/unlit.vert", "programs/unlit.frag");

    juce::String assetPackError;
    if (! assets::EngineAssetPack::ensureInstalled(assetPackError) ||
        ! assetCatalog_.LoadAssetPack(assets::EngineAssetPack::packId, assets::EngineAssetPack::version, assetPackError)) {
        std::cout << "[render] could not load the Creation Engine Pack: " << assetPackError << std::endl;
        return;
    }

    SeedDemoScene();

    gridRenderer_.Build(10.0f, 1.0f);
    gridProgram_ = shaderComposer_->GetProgram(openGLContext_, "programs/grid.vert", "programs/grid.frag");

    LogGLErrors("catalog load + scene seed + grid");

    lastFrameTimeSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::cout << "[render] newOpenGLContextCreated: done" << std::endl;
}

void ViewportComponent::renderOpenGL() {
    bool vrFrameActive = false;
    if (openXRProvider_ != nullptr) {
        vrFrameActive = openXRProvider_->beginFrame(vrFrame_);
    }
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
    if (vrFrameActive && vrFrame_.leftEye.renderTarget != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(vrFrame_.leftEye.renderTarget));
        glViewport(0, 0, static_cast<GLsizei>(vrFrame_.leftEye.renderWidth),
                   static_cast<GLsizei>(vrFrame_.leftEye.renderHeight));
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, juce::roundToInt(scale * static_cast<float>(getWidth())),
                   juce::roundToInt(scale * static_cast<float>(getHeight())));
    }

    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (shaderComposer_ == nullptr) {
        if (vrFrameActive) openXRProvider_->submitFrame(vrFrame_);
        return;
    }

    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const float deltaSeconds = static_cast<float>(nowSeconds - lastFrameTimeSeconds_);
    lastFrameTimeSeconds_ = nowSeconds;
    freeCamera_.Update(deltaSeconds);
    if (vrFrameActive) {
        updateVREditorRig(deltaSeconds);
        updateVRInteraction();
        uploadVRWands();
        uploadVRTransformGizmo();
        uploadVREditorCart();
    } else {
        updateDesktopTransformGizmo();
        uploadVRTransformGizmo();
    }

    const float aspect = getHeight() > 0 ? static_cast<float>(getWidth()) / static_cast<float>(getHeight()) : 1.0f;
    camera_.SetPerspective(juce::MathConstants<float>::pi / 4.0f, aspect, 0.1f, 100.0f);
    const auto cameraPos = freeCamera_.Position();
    const auto cameraTarget = freeCamera_.Target();
    camera_.SetLookAt(cameraPos, cameraTarget);
    if (vrFrameActive) {
        ConfigureVRCamera(camera_, vrFrame_.leftEye, vrRigPosition_, vrRigYaw_, vrRigPitch_);
    }

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
    auto drawVRWands = [&](const Camera& eyeCamera) {
        if (!vrFrameActive || vrWandVertexCount_ == 0 || gridProgram_ == nullptr) return;
        glDisable(GL_DEPTH_TEST);
        gridProgram_->use();
        gridProgram_->setUniformMat4("uView", eyeCamera.ViewMatrix().mat, 1, GL_FALSE);
        gridProgram_->setUniformMat4("uProjection", eyeCamera.ProjectionMatrix().mat, 1, GL_FALSE);
        vrWandVertexArray_.Bind();
        for (std::size_t index = 0; index < vrWands_.size(); ++index) {
            const auto& wand = vrWands_[index];
            if (!wand.active) continue;
            // A bright, thick handle makes the tool itself visible; the
            // thinner coloured line is the interaction ray from its tip.
            glLineWidth(12.0f);
            gridProgram_->setUniform("uColor", 0.92f, 0.94f, 1.0f);
            glDrawArrays(GL_LINES, static_cast<GLint>(index * 4), 2);
            glLineWidth(3.0f);
            gridProgram_->setUniform("uColor", wand.hit ? 1.0f : 0.25f, wand.hit ? 0.8f : 0.75f, 0.15f);
            glDrawArrays(GL_LINES, static_cast<GLint>(index * 4 + 2), 2);
        }
        gl::VertexArray::Unbind();
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    };
    auto drawVRTransformGizmo = [&](const Camera& eyeCamera) {
        if (vrGizmoVertexCount_ == 0 || gridProgram_ == nullptr) return;
        glDisable(GL_DEPTH_TEST);
        gridProgram_->use();
        gridProgram_->setUniformMat4("uView", eyeCamera.ViewMatrix().mat, 1, GL_FALSE);
        gridProgram_->setUniformMat4("uProjection", eyeCamera.ProjectionMatrix().mat, 1, GL_FALSE);
        vrGizmoVertexArray_.Bind();
        if (vrTransformGizmo_.entity != entt::null) {
            glLineWidth(7.0f);
            gridProgram_->setUniform("uColor", 0.95f, 0.16f, 0.14f); glDrawArrays(GL_LINES, 0, 6);
            gridProgram_->setUniform("uColor", 0.16f, 0.92f, 0.22f); glDrawArrays(GL_LINES, 6, 6);
            gridProgram_->setUniform("uColor", 0.18f, 0.42f, 1.0f); glDrawArrays(GL_LINES, 12, 6);
            glLineWidth(3.0f);
            gridProgram_->setUniform("uColor", 0.92f, 0.92f, 0.95f); glDrawArrays(GL_LINES, 18, 24);
            gridProgram_->setUniform("uColor", 1.0f, 0.82f, 0.18f); glDrawArrays(GL_LINES, 42, 24);
        }
        if (vrHoverActive_) {
            glLineWidth(5.0f);
            gridProgram_->setUniform("uColor", 1.0f, 0.76f, 0.10f);
            glDrawArrays(GL_LINES, 66, vrGizmoVertexCount_ - 66);
        }
        gl::VertexArray::Unbind();
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    };
    auto drawVREditorCart = [&](const Camera& eyeCamera) {
        if (!vrFrameActive || vrCartVertexCount_ == 0 || gridProgram_ == nullptr) return;
        glEnable(GL_DEPTH_TEST);
        gridProgram_->use();
        gridProgram_->setUniformMat4("uView", eyeCamera.ViewMatrix().mat, 1, GL_FALSE);
        gridProgram_->setUniformMat4("uProjection", eyeCamera.ProjectionMatrix().mat, 1, GL_FALSE);
        vrCartVertexArray_.Bind();
        gridProgram_->setUniform("uColor", 0.05f, 0.30f, 0.38f);
        glDrawArrays(GL_TRIANGLES, 0, vrCartDeckVertexCount_);
        gridProgram_->setUniform("uColor", 0.08f, 0.82f, 0.95f);
        glLineWidth(5.0f);
        glDrawArrays(GL_LINES, vrCartDeckVertexCount_, vrCartVertexCount_ - vrCartDeckVertexCount_);
        gl::VertexArray::Unbind();
        glLineWidth(1.0f);
    };

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
    // function. entt::registry isn't internally thread-safe: authoring
    // panels on the message thread genuinely touch the same registry
    // concurrently with this render loop.
    const std::lock_guard<std::mutex> registryLock(world_.RegistryMutex());

    // Demo-only: spins the seeded demo entity's Transform based on the
    // World's tick count rather than wall-clock time, so the spin
    // actually stops when the transport is paused/stopped (World::
    // AdvanceTick() is gated on play state in MainComponent's timer) —
    // an editor "playing" a paused simulation should look paused. Only
    // writes when the tick has actually changed since the last frame
    // (not unconditionally every frame): the inspector edits the same
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
        if (const auto* existing = world_.Registry().try_get<scene::MeshRenderer>(entity);
            existing != nullptr && existing->mesh != nullptr) continue;
        const auto& reference = unresolvedAssets.get<const scene::MeshAssetReference>(entity);
        const auto asset = assetCatalog_.Find(reference.packId.isNotEmpty()
            ? scene::AssetCatalog::PackAssetKey(reference.packId, reference.packVersion, reference.assetId)
            : reference.assetId);
        if (asset.mesh != nullptr && asset.material != nullptr) {
            if (auto* existing = world_.Registry().try_get<scene::MeshRenderer>(entity))
                existing->mesh = asset.mesh;
            else
                world_.Registry().emplace<scene::MeshRenderer>(entity, scene::MeshRenderer{ asset.mesh, asset.material });
        }
    }

    auto drawMeshes = [&](const Camera& eyeCamera, const juce::Vector3D<float>& eyePosition) {
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
        program->setUniformMat4("uView", eyeCamera.ViewMatrix().mat, 1, GL_FALSE);
        program->setUniformMat4("uProjection", eyeCamera.ProjectionMatrix().mat, 1, GL_FALSE);
        program->setUniformMat3("uNormalMatrix", normalMatrix.data(), 1, GL_FALSE);
        program->setUniform("uCameraPos", eyePosition.x, eyePosition.y, eyePosition.z);

        // Runtime tint overrides leave the shared material unchanged.
        juce::Vector3D<float> tint{ 1.0f, 1.0f, 1.0f };
        if (const auto* runtimeTint = world_.Registry().try_get<const engine::Tint>(entity)) {
            tint = { runtimeTint->color.x, runtimeTint->color.y, runtimeTint->color.z };
        }
        if (entity == interactions_.selected()) {
            tint = { 1.0f, 0.62f, 0.12f };
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
    };

    drawMeshes(camera_, camera_.Position());
    drawVREditorCart(camera_);
    drawVRWands(camera_);
    drawVRTransformGizmo(camera_);

    if (vrFrameActive) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(vrFrame_.rightEye.renderTarget));
        glViewport(0, 0, static_cast<GLsizei>(vrFrame_.rightEye.renderWidth),
                   static_cast<GLsizei>(vrFrame_.rightEye.renderHeight));
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ConfigureVRCamera(camera_, vrFrame_.rightEye, vrRigPosition_, vrRigYaw_, vrRigPitch_);
        if (gridProgram_ != nullptr) {
            gridProgram_->use();
            gridProgram_->setUniformMat4("uView", camera_.ViewMatrix().mat, 1, GL_FALSE);
            gridProgram_->setUniformMat4("uProjection", camera_.ProjectionMatrix().mat, 1, GL_FALSE);
            gridProgram_->setUniform("uColor", 0.2f, 0.24f, 0.32f);
            gridRenderer_.Draw();
        }
        drawMeshes(camera_, camera_.Position());
        drawVREditorCart(camera_);
        drawVRWands(camera_);
        drawVRTransformGizmo(camera_);
    }

    LogGLErrors("draw");
    if (vrFrameActive) openXRProvider_->submitFrame(vrFrame_);
}

void ViewportComponent::openGLContextClosing() {
    if (openXRProvider_ != nullptr) {
        openXRProvider_->shutdown();
        openXRProvider_.reset();
    }
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
