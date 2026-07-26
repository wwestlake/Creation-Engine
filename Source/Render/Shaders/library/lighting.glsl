#include "library/pbr_brdf.glsl"

// Light accumulation chunk: turns a light description into a radiance
// contribution via the Cook-Torrance BRDF above. One directional light
// (the "sun") plus point lights covers the M2 lighting requirement;
// spot lights and shadowing are future work.

struct DirectionalLight {
    vec3 direction; // direction the light travels, i.e. surface-to-light = -direction.
    vec3 color;
    float intensity;
};

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
};

vec3 AccumulateDirectionalLight(DirectionalLight light, vec3 N, vec3 V, vec3 albedo, float metallic,
                                 float roughness) {
    vec3 L = normalize(-light.direction);
    vec3 radiance = light.color * light.intensity;
    return CookTorranceBRDF(N, V, L, albedo, metallic, roughness, radiance);
}

vec3 AccumulatePointLight(PointLight light, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic,
                           float roughness) {
    vec3 toLight = light.position - worldPos;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 0.0001);
    float attenuation = 1.0 / max(dist * dist, 0.0001);
    vec3 radiance = light.color * light.intensity * attenuation;
    return CookTorranceBRDF(N, V, L, albedo, metallic, roughness, radiance);
}
