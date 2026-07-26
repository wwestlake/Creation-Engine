#version 410 core
#include "library/lighting.glsl"
#include "library/texture_sampling.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uCameraPos;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;

#ifdef USE_ALBEDO_TEXTURE
uniform sampler2D uAlbedoTexture;
#endif

uniform DirectionalLight uSunLight;
uniform PointLight uPointLight;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 albedo = uAlbedo;
#ifdef USE_ALBEDO_TEXTURE
    albedo = SampleAlbedo(uAlbedoTexture, vUV).rgb;
#endif

    vec3 color = vec3(0.0);
    color += AccumulateDirectionalLight(uSunLight, N, V, albedo, uMetallic, uRoughness);
    color += AccumulatePointLight(uPointLight, vWorldPos, N, V, albedo, uMetallic, uRoughness);

    // Small constant ambient term so unlit-facing areas aren't pure black
    // until real image-based lighting exists (explicitly deferred).
    color += albedo * 0.03;

    // Reinhard tonemap + gamma correction for display.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
