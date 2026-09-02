#version 410 core
#include "library/lighting.glsl"

// Must match ce::kMaxPointLights (Source/Render/Scene/Light.h) — the
// editable light list is capped at the same size as this array.
#define CE_MAX_POINT_LIGHTS 4

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uCameraPos;
uniform DirectionalLight uSunLight;
uniform PointLight uPointLights[CE_MAX_POINT_LIGHTS];
uniform int uPointLightCount;
uniform float uTime;

out vec4 fragColor;

// Filled by ShaderComposer with a compiled material graph's declarations
// (parameter/texture uniforms, plus a RotateUV/NormalFromHeight helper if
// the graph uses a Rotator/Normal From Height node) and its
// EvaluateMaterial(in vec2, in vec3, in vec3, in vec3, in float, out
// vec3, out float, out float, out vec3) function (see
// ce::material::CompileMaterialGraph) — same "host owns lighting, the
// graph owns surface appearance" split pbr_lit.frag started with a fixed
// material baked in; this is that same lighting code with the fixed part
// replaced by whatever graph is bound. The three extra inputs (world
// position, world normal, camera vector) are just geometricNormal/V/
// vWorldPos already computed below, handed to the graph as plain
// parameters -- UE's World Position/Pixel Normal WS/Camera Vector WS
// nodes need the same per-pixel values this shader already has, just not
// exposed to the graph before. The graph's own `normal` OUTPUT then
// feeds lighting below instead of the raw geometric one, unmodified
// unless something (e.g. Normal From Height) actually wrote to it.
//$MATERIAL_SOURCE$

void main() {
    vec3 geometricNormal = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 albedo;
    float metallic;
    float roughness;
    vec3 N;
    EvaluateMaterial(vUV, vWorldPos, geometricNormal, V, uTime, albedo, metallic, roughness, N);
    N = normalize(N);

    vec3 color = vec3(0.0);
    color += AccumulateDirectionalLight(uSunLight, N, V, albedo, metallic, roughness);

    int activeLights = min(uPointLightCount, CE_MAX_POINT_LIGHTS);
    for (int i = 0; i < activeLights; ++i) {
        color += AccumulatePointLight(uPointLights[i], vWorldPos, N, V, albedo, metallic, roughness);
    }

    // Small constant ambient term so unlit-facing areas aren't pure black
    // until real image-based lighting exists (explicitly deferred) --
    // same as pbr_lit.frag, this is host/lighting policy, not material.
    color += albedo * 0.03;

    // Reinhard tonemap + gamma correction for display.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
