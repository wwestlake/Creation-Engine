#include "Interaction/EditorInteraction.h"

#include <cmath>

namespace ce::interaction {

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
    const auto& a = edit.before.position;
    const auto& b = edit.after.position;
    if (a.x == b.x && a.y == b.y && a.z == b.z) return;
    undoStack_.push_back(edit);
    redoStack_.clear();
}

bool EditorInteraction::beginTranslation(entt::entity entity, const engine::Vec3& controllerWorldPosition,
                                         TranslationConstraint constraint)
{
    std::lock_guard<std::mutex> stateLock(stateMutex_);
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(entity) || !registry.all_of<scene::Transform>(entity)) return false;
    if (const auto* flags = registry.try_get<scene::SceneFlags>(entity); flags != nullptr && flags->locked) return false;
    activeGrab_ = TransformEdit{ entity, registry.get<scene::Transform>(entity), registry.get<scene::Transform>(entity) };
    grabControllerStart_ = controllerWorldPosition;
    translationConstraint_ = constraint;
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
    if (!translationConstraint_.x) delta.x = 0.0f;
    if (!translationConstraint_.y) delta.y = 0.0f;
    if (!translationConstraint_.z) delta.z = 0.0f;
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

bool EditorInteraction::apply(const TransformEdit& edit, bool useAfter)
{
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(edit.entity) || !registry.all_of<scene::Transform>(edit.entity)) return false;
    registry.get<scene::Transform>(edit.entity) = useAfter ? edit.after : edit.before;
    return true;
}

} // namespace ce::interaction
