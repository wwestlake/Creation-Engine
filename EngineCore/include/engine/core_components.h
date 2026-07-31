#pragma once

#include "engine/math.h"

namespace ce::engine {

// Framework-agnostic transform component. The scene-composition layer's
// own ce::scene::Transform (Source/Scene/Components.h) is JUCE-facing
// (juce::Vector3D<float>) and unreachable from CreationEngineServer or
// EngineCore; this is the type CEL scripts' get_position/set_position/
// ... intrinsics (Language/src/jit/intrinsic_trampolines.cpp) actually
// read and write. Same field shape as ce::scene::Transform on purpose,
// so the GS6 migration (folding scene::Transform's data into this type,
// converting to/from juce::Vector3D<float> only at the render/UI
// boundary) is mechanical rather than a redesign.
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
// graph actually calls set_color. This is what CEL's get_color/
// set_color intrinsics (Language/src/jit/intrinsic_trampolines.cpp)
// read and write; the render boundary (ViewportComponent) multiplies it
// into Material::ApplyUniforms's albedo at draw time.
struct Tint {
    Vec3 color{ 1.0f, 1.0f, 1.0f };
};

} // namespace ce::engine
