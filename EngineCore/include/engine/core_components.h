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

} // namespace ce::engine
