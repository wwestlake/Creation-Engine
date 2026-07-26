#pragma once

#include <JuceHeader.h>

namespace ce {

// Plain editable light data — mirrors the DirectionalLight/PointLight
// GLSL structs in Source/Render/Shaders/library/lighting.glsl field for
// field, so uploading one to the shader is a direct copy, no translation.
struct DirectionalLight {
    juce::Vector3D<float> direction{ -0.4f, -1.0f, -0.3f };
    juce::Vector3D<float> color{ 1.0f, 0.97f, 0.9f };
    float intensity = 3.0f;
};

struct PointLight {
    juce::Vector3D<float> position{ 2.0f, 1.5f, 2.0f };
    juce::Vector3D<float> color{ 0.3f, 0.5f, 1.0f };
    float intensity = 8.0f;
};

// Matches CE_MAX_POINT_LIGHTS in programs/pbr_lit.frag — the shader's
// uPointLights[] array is fixed-size, so the editable light list is
// capped here rather than failing silently past the array bound.
constexpr int kMaxPointLights = 4;

} // namespace ce
