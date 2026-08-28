#pragma once

namespace ce::engine {

// Plain, framework-agnostic 3-component vector -- deliberately NOT
// juce::Vector3D<float> (JUCE lives one layer up, in the editor/server
// executables, never in EngineCore). This is the shared POD representation
// used by engine components and host capabilities.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace ce::engine
