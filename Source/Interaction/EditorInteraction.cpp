#include "Interaction/EditorInteraction.h"

#include <algorithm>
#include <cmath>

namespace ce::interaction {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
}

void EditorInteraction::select(entt::entity entity)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (selected_ == entity) return;
    selected_ = entity;
    pendingSelection_ = entity;
}

entt::entity EditorInteraction::selected() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return selected_;
}

std::optional<entt::entity> EditorInteraction::takeSelectionChange()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto change = pendingSelection_;
    pendingSelection_.reset();
    return change;
}

bool EditorInteraction::beginGrab(entt::entity entity, float hitDistance)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(entity) || !registry.all_of<scene::Transform>(entity)) return false;
    if (const auto* flags = registry.try_get<scene::SceneFlags>(entity); flags != nullptr && flags->locked) return false;
    activeGrab_ = TransformEdit{ entity, registry.get<scene::Transform>(entity), registry.get<scene::Transform>(entity) };
    grabDistance_ = hitDistance;
    if (selected_ != entity) {
        selected_ = entity;
        pendingSelection_ = entity;
    }
    return true;
}

void EditorInteraction::updateGrab(const engine::Vec3& worldPosition)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    if (!activeGrab_) return;
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(activeGrab_->entity) || !registry.all_of<scene::Transform>(activeGrab_->entity)) {
        activeGrab_.reset();
        return;
    }
    auto& transform = registry.get<scene::Transform>(activeGrab_->entity);
    transform.position = snapped(worldPosition);
    activeGrab_->after = transform;
}

void EditorInteraction::endGrab()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!activeGrab_) return;
    const auto edit = *activeGrab_;
    activeGrab_.reset();
    const auto& a = edit.before;
    const auto& b = edit.after;
    // Compares position, rotation, AND scale now (used to be position-only,
    // back when this class only ever moved things) -- a scale or rotate
    // drag that ends up back where it started shouldn't push a no-op onto
    // the undo stack either.
    const bool unchanged = a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
                           a.eulerRotationRadians.x == b.eulerRotationRadians.x &&
                           a.eulerRotationRadians.y == b.eulerRotationRadians.y &&
                           a.eulerRotationRadians.z == b.eulerRotationRadians.z &&
                           a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
    if (unchanged) return;
    undoStack_.push_back(edit);
    redoStack_.clear();
}

bool EditorInteraction::beginScale(entt::entity entity, int axisIndex, const engine::Vec3& controllerWorldPosition)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(entity) || !registry.all_of<scene::Transform>(entity)) return false;
    if (const auto* flags = registry.try_get<scene::SceneFlags>(entity); flags != nullptr && flags->locked) return false;
    const auto& transform = registry.get<scene::Transform>(entity);
    activeGrab_ = TransformEdit{ entity, transform, transform };
    scaleAxisIndex_ = axisIndex;
    const engine::Vec3 offset{ controllerWorldPosition.x - transform.position.x,
                               controllerWorldPosition.y - transform.position.y,
                               controllerWorldPosition.z - transform.position.z };
    scaleAxisDistance0_ = axisIndex == 0 ? offset.x
                         : axisIndex == 1 ? offset.y
                         : axisIndex == 2 ? offset.z
                         : std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    // Guard against a near-zero starting distance (grabbed almost exactly
    // at the gizmo centre) producing a huge or divide-by-zero ratio later.
    if (std::abs(scaleAxisDistance0_) < 0.05f)
        scaleAxisDistance0_ = scaleAxisDistance0_ < 0.0f ? -0.05f : 0.05f;
    if (selected_ != entity) {
        selected_ = entity;
        pendingSelection_ = entity;
    }
    return true;
}

void EditorInteraction::updateScale(const engine::Vec3& controllerWorldPosition)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    if (!activeGrab_) return;
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(activeGrab_->entity) || !registry.all_of<scene::Transform>(activeGrab_->entity)) {
        activeGrab_.reset();
        return;
    }
    const auto& center = activeGrab_->before.position;
    const engine::Vec3 offset{ controllerWorldPosition.x - center.x,
                               controllerWorldPosition.y - center.y,
                               controllerWorldPosition.z - center.z };
    const float currentDistance = scaleAxisIndex_ == 0 ? offset.x
                                 : scaleAxisIndex_ == 1 ? offset.y
                                 : scaleAxisIndex_ == 2 ? offset.z
                                 : std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    const float ratio = std::max(0.01f, currentDistance / scaleAxisDistance0_);
    auto& transform = registry.get<scene::Transform>(activeGrab_->entity);
    transform.scale = activeGrab_->before.scale;
    if (scaleAxisIndex_ < 0) {
        transform.scale.x *= ratio;
        transform.scale.y *= ratio;
        transform.scale.z *= ratio;
    } else if (scaleAxisIndex_ == 0) {
        transform.scale.x *= ratio;
    } else if (scaleAxisIndex_ == 1) {
        transform.scale.y *= ratio;
    } else if (scaleAxisIndex_ == 2) {
        transform.scale.z *= ratio;
    }
    activeGrab_->after = transform;
}

bool EditorInteraction::beginRotation(entt::entity entity, int axisIndex, float angleRadians)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(entity) || !registry.all_of<scene::Transform>(entity)) return false;
    if (const auto* flags = registry.try_get<scene::SceneFlags>(entity); flags != nullptr && flags->locked) return false;
    const auto& transform = registry.get<scene::Transform>(entity);
    activeGrab_ = TransformEdit{ entity, transform, transform };
    rotationAxisIndex_ = axisIndex;
    rotationAngle0_ = angleRadians;
    if (selected_ != entity) {
        selected_ = entity;
        pendingSelection_ = entity;
    }
    return true;
}

void EditorInteraction::updateRotation(float angleRadians)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    if (!activeGrab_) return;
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(activeGrab_->entity) || !registry.all_of<scene::Transform>(activeGrab_->entity)) {
        activeGrab_.reset();
        return;
    }
    float delta = angleRadians - rotationAngle0_;
    // Wrap to the shortest angular path so crossing atan2's +-pi seam
    // doesn't read as a huge spurious jump in either direction.
    while (delta > kPi) delta -= kTwoPi;
    while (delta < -kPi) delta += kTwoPi;
    delta = snappedAngle(delta);
    auto& transform = registry.get<scene::Transform>(activeGrab_->entity);
    transform.eulerRotationRadians = activeGrab_->before.eulerRotationRadians;
    if (rotationAxisIndex_ == 0) transform.eulerRotationRadians.x += delta;
    else if (rotationAxisIndex_ == 1) transform.eulerRotationRadians.y += delta;
    else if (rotationAxisIndex_ == 2) transform.eulerRotationRadians.z += delta;
    activeGrab_->after = transform;
}

bool EditorInteraction::beginTranslation(entt::entity entity, const engine::Vec3& controllerWorldPosition,
                                         TranslationConstraint constraint, std::optional<engine::Vec3> localAxis)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(entity) || !registry.all_of<scene::Transform>(entity)) return false;
    if (const auto* flags = registry.try_get<scene::SceneFlags>(entity); flags != nullptr && flags->locked) return false;
    activeGrab_ = TransformEdit{ entity, registry.get<scene::Transform>(entity), registry.get<scene::Transform>(entity) };
    grabControllerStart_ = controllerWorldPosition;
    translationConstraint_ = constraint;
    localAxisOverride_ = localAxis;
    if (selected_ != entity) {
        selected_ = entity;
        pendingSelection_ = entity;
    }
    return true;
}

void EditorInteraction::updateTranslation(const engine::Vec3& controllerWorldPosition)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    if (!activeGrab_) return;
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(activeGrab_->entity) || !registry.all_of<scene::Transform>(activeGrab_->entity)) {
        activeGrab_.reset();
        return;
    }
    engine::Vec3 delta{
        controllerWorldPosition.x - grabControllerStart_.x,
        controllerWorldPosition.y - grabControllerStart_.y,
        controllerWorldPosition.z - grabControllerStart_.z
    };
    if (localAxisOverride_) {
        // Project the raw delta onto the single axis direction beginTranslation
        // was given -- identical math whether that direction happens to be a
        // world unit axis or the entity's own rotated local basis vector.
        const auto& axis = *localAxisOverride_;
        const float projected = delta.x * axis.x + delta.y * axis.y + delta.z * axis.z;
        delta = { axis.x * projected, axis.y * projected, axis.z * projected };
    } else {
        if (!translationConstraint_.x) delta.x = 0.0f;
        if (!translationConstraint_.y) delta.y = 0.0f;
        if (!translationConstraint_.z) delta.z = 0.0f;
    }
    // `delta` is a world-space displacement; `before.position`/`transform.position`
    // are local (relative to Parent, per docs/OBJECT_MODEL.md). Adding a
    // world delta straight to a local position is only exactly correct
    // when nothing in this entity's Parent chain rotates or scales it --
    // true for the common case (a plain dropped object, or a mesh part
    // under an unrotated definition root), not yet correct in general (a
    // rotated parent would skew the delta's axes relative to local space).
    // Accepted limitation for now, not silently assumed away: a fully
    // general fix would convert `delta` into the parent's current local
    // frame (same inverse-rotation approach HierarchyPanel::Reparent
    // already uses for preserving world position across a reparent).
    auto& transform = registry.get<scene::Transform>(activeGrab_->entity);
    transform.position = snapped({
        activeGrab_->before.position.x + delta.x,
        activeGrab_->before.position.y + delta.y,
        activeGrab_->before.position.z + delta.z
    });
    activeGrab_->after = transform;
}

bool EditorInteraction::undo()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (undoStack_.empty()) return false;
    const auto edit = undoStack_.back();
    if (!apply(edit, false)) return false;
    undoStack_.pop_back();
    redoStack_.push_back(edit);
    selected_ = edit.entity;
    pendingSelection_ = edit.entity;
    return true;
}

bool EditorInteraction::redo()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (redoStack_.empty()) return false;
    const auto edit = redoStack_.back();
    if (!apply(edit, true)) return false;
    redoStack_.pop_back();
    undoStack_.push_back(edit);
    selected_ = edit.entity;
    pendingSelection_ = edit.entity;
    return true;
}

engine::Vec3 EditorInteraction::snapped(const engine::Vec3& position) const
{
    if (!snapSettings_.enabled || snapSettings_.translationMeters <= 0.0f) return position;
    const auto snap = snapSettings_.translationMeters;
    return { std::round(position.x / snap) * snap, std::round(position.y / snap) * snap, std::round(position.z / snap) * snap };
}

float EditorInteraction::snappedAngle(float radians) const
{
    if (!angleSnapSettings_.enabled || angleSnapSettings_.degrees <= 0.0f) return radians;
    const float snapRadians = angleSnapSettings_.degrees * (kPi / 180.0f);
    return std::round(radians / snapRadians) * snapRadians;
}

bool EditorInteraction::apply(const TransformEdit& edit, bool useAfter)
{
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(edit.entity) || !registry.all_of<scene::Transform>(edit.entity)) return false;
    registry.get<scene::Transform>(edit.entity) = useAfter ? edit.after : edit.before;
    return true;
}

} // namespace ce::interaction
