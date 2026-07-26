#version 410 core
#include "library/lighting.glsl"

in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uCameraPos;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;

uniform DirectionalLight uSunLight;
uniform PointLight uPointLight;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 color = vec3(0.0);
    color += AccumulateDirectionalLight(uSunLight, N, V, uAlbedo, uMetallic, uRoughness);
    color += AccumulatePointLight(uPointLight, vWorldPos, N, V, uAlbedo, uMetallic, uRoughness);

    // Small constant ambient term so unlit-facing areas aren't pure black
    // until real image-based lighting exists (explicitly deferred).
    color += uAlbedo * 0.03;

    // Reinhard tonemap + gamma correction for display.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
