#pragma once

#include <optional>
#include <vector>

#include <entt/entt.hpp>

#include "engine/world.h"
#include "Scene/Components.h"

namespace ce::interaction {

enum class LocomotionMode : std::uint8_t { seatedFly, groundedWalk };
enum class TransformSpace : std::uint8_t { world, local };

struct SnapSettings {
    bool enabled = true;
    float translationMeters = 0.25f;
};

// A translation handle constrains the physical-controller delta to one
// world axis, one world plane, or all three world axes at once.
struct TranslationConstraint {
    bool x = true;
    bool y = true;
    bool z = true;
};

// The single owner of authoring interaction state. Desktop tools, VR wands,
// and future spatial panels all select and transform the same World entities
// through this class, which keeps manipulation undoable and snap-aware.
class EditorInteraction final {
public:
    explicit EditorInteraction(engine::World& world) : world_(world) {}

    void select(entt::entity entity);
    [[nodiscard]] entt::entity selected() const;
    [[nodiscard]] std::optional<entt::entity> takeSelectionChange();

    void setLocomotionMode(LocomotionMode mode) noexcept { locomotionMode_ = mode; }
    [[nodiscard]] LocomotionMode locomotionMode() const noexcept { return locomotionMode_; }
    void setTransformSpace(TransformSpace space) noexcept { transformSpace_ = space; }
    [[nodiscard]] TransformSpace transformSpace() const noexcept { return transformSpace_; }
    SnapSettings& snapSettings() noexcept { return snapSettings_; }
    const SnapSettings& snapSettings() const noexcept { return snapSettings_; }

    bool beginGrab(entt::entity entity, float hitDistance);
    void updateGrab(const engine::Vec3& worldPosition);
    void endGrab();

    bool beginTranslation(entt::entity entity, const engine::Vec3& controllerWorldPosition,
                          TranslationConstraint constraint);
    void updateTranslation(const engine::Vec3& controllerWorldPosition);
    bool undo();
    bool redo();

private:
    struct TransformEdit { entt::entity entity = entt::null; scene::Transform before, after; };

    [[nodiscard]] engine::Vec3 snapped(const engine::Vec3& position) const;
    bool apply(const TransformEdit& edit, bool useAfter);

    engine::World& world_;
    mutable std::mutex stateMutex_;
    entt::entity selected_ = entt::null;
    std::optional<entt::entity> pendingSelection_;
    LocomotionMode locomotionMode_ = LocomotionMode::seatedFly;
    TransformSpace transformSpace_ = TransformSpace::world;
    SnapSettings snapSettings_;
    std::optional<TransformEdit> activeGrab_;
    float grabDistance_ = 0.0f;
    engine::Vec3 grabControllerStart_;
    TranslationConstraint translationConstraint_;
    std::vector<TransformEdit> undoStack_;
    std::vector<TransformEdit> redoStack_;
};

} // namespace ce::interaction
#include <mutex>
