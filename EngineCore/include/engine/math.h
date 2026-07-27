#pragma once

namespace ce::engine {

// Plain, framework-agnostic 3-component vector -- deliberately NOT
// juce::Vector3D<float> (JUCE lives one layer up, in the editor/server
// executables, never in EngineCore) and deliberately a distinct type
// from CEL's own IR-level vec3 (an LLVM struct, Language/src/jit/
// module_builder.cpp -- Language/ stays independent of EngineCore's
// exact layout too). This is the one shared POD representation both
// sides marshal through at the host ABI boundary
// (Language/src/jit/intrinsic_trampolines.cpp) and that engine
// components (core_components.h) are built from.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace ce::engine
