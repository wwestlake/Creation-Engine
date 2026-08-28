#pragma once

#include "engine/math.h"

namespace ce::engine {

// Framework-agnostic transform component. The scene-composition layer's
// own ce::scene::Transform (Source/Scene/Components.h) is JUCE-facing
// (juce::Vector3D<float>) and unreachable from CreationEngineServer or
// EngineCore. It is shared with the scene composition layer through a type
// alias so host capabilities and editor controls update one component pool.
struct Transform {
    Vec3 position;
    Vec3 eulerRotationRadians;
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
};

// Per-entity runtime color multiplier, applied on top of whatever
// Material a MeshRenderer references (Render/Scene/Material.h) rather
// than mutating the Material itself -- a Material is a shared asset
// (MeshRenderer's own comment: "several entities can point at the same
// Mesh/Material without duplicating GPU resources"), so a script tinting
// one entity red must never bleed into every other entity sharing that
// Material. Same "share the immutable asset data, not the per-instance
// state" split as Animator vs its shared clip data. Defaults to
// {1,1,1}: identity multiply, i.e. no visible change until a script/node
// graph updates it. The render boundary multiplies it into the material
// albedo at draw time.
struct Tint {
    Vec3 color{ 1.0f, 1.0f, 1.0f };
};

} // namespace ce::engine
