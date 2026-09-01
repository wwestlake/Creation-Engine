#pragma once

#include <optional>
#include <vector>

#include <entt/entt.hpp>

#include "engine/world.h"
#include "Scene/Components.h"

namespace ce::interaction {

enum class LocomotionMode : std::uint8_t { seatedFly, groundedWalk };
enum class TransformSpace : std::uint8_t { world, local };

// Which gizmo the viewport currently shows/lets you drag for the
// selected entity -- one at a time, switched from the Transform panel
// (a plain one-shot click, not a continuous per-frame input, so no
// threading concerns the way dragging itself has).
enum class GizmoMode : std::uint8_t { position, scale, rotate };

struct SnapSettings {
    bool enabled = true;
    float translationMeters = 0.25f;
};

struct AngleSnapSettings {
    bool enabled = false;
    float degrees = 15.0f;
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
    AngleSnapSettings& angleSnapSettings() noexcept { return angleSnapSettings_; }
    const AngleSnapSettings& angleSnapSettings() const noexcept { return angleSnapSettings_; }

    void setGizmoMode(GizmoMode mode) noexcept { gizmoMode_ = mode; }
    [[nodiscard]] GizmoMode gizmoMode() const noexcept { return gizmoMode_; }

    bool beginGrab(entt::entity entity, float hitDistance);
    void updateGrab(const engine::Vec3& worldPosition);
    void endGrab();

    // localAxis, when set, overrides `constraint` entirely: the drag is
    // projected onto that single direction instead of masking world
    // x/y/z components. There is nothing "local" baked into this class
    // about that direction -- it works identically whether the caller
    // passes a world unit axis or the selected entity's own rotated
    // basis vector; ViewportComponent decides which by reading
    // transformSpace() before calling this.
    bool beginTranslation(entt::entity entity, const engine::Vec3& controllerWorldPosition,
                          TranslationConstraint constraint, std::optional<engine::Vec3> localAxis = std::nullopt);
    void updateTranslation(const engine::Vec3& controllerWorldPosition);

    // axisIndex: 0/1/2 = X/Y/Z only, -1 = uniform (all three together,
    // the free-move cube's scale-mode equivalent).
    bool beginScale(entt::entity entity, int axisIndex, const engine::Vec3& controllerWorldPosition);
    void updateScale(const engine::Vec3& controllerWorldPosition);

    // axisIndex: 0/1/2 = rotate around X/Y/Z. angleRadians is the
    // caller's already-computed current angle around that axis (e.g.
    // via a ray/plane intersection done in viewport space) -- this
    // class only tracks the delta from the angle beginRotation captured,
    // the same "caller solves the geometry, this class just applies the
    // result" split beginTranslation/updateTranslation already use.
    bool beginRotation(entt::entity entity, int axisIndex, float angleRadians);
    void updateRotation(float angleRadians);

    bool undo();
    bool redo();

private:
    struct TransformEdit { entt::entity entity = entt::null; scene::Transform before, after; };

    [[nodiscard]] engine::Vec3 snapped(const engine::Vec3& position) const;
    [[nodiscard]] float snappedAngle(float radians) const;
    bool apply(const TransformEdit& edit, bool useAfter);

    engine::World& world_;
    mutable std::mutex stateMutex_;
    entt::entity selected_ = entt::null;
    std::optional<entt::entity> pendingSelection_;
    LocomotionMode locomotionMode_ = LocomotionMode::seatedFly;
    TransformSpace transformSpace_ = TransformSpace::world;
    GizmoMode gizmoMode_ = GizmoMode::position;
    SnapSettings snapSettings_;
    AngleSnapSettings angleSnapSettings_;
    std::optional<TransformEdit> activeGrab_;
    float grabDistance_ = 0.0f;
    engine::Vec3 grabControllerStart_;
    TranslationConstraint translationConstraint_;
    std::optional<engine::Vec3> localAxisOverride_;

    int scaleAxisIndex_ = -1;
    float scaleAxisDistance0_ = 0.0f;

    int rotationAxisIndex_ = -1;
    float rotationAngle0_ = 0.0f;

    std::vector<TransformEdit> undoStack_;
    std::vector<TransformEdit> redoStack_;
};

} // namespace ce::interaction
#include <mutex>
