#include "library/common.glsl"

// Cook-Torrance metallic-roughness BRDF (GGX distribution, Smith
// geometry, Schlick Fresnel) — the standard PBR model, matching the
// albedo/roughness/metallic channel set the capabilities spec commits
// materials to.

const float PBR_PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PBR_PI * denom * denom;
    return a2 / max(denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(Saturate(1.0 - cosTheta), 5.0);
}

// Outgoing radiance contributed by one light arriving from direction L
// with incoming radiance `lightRadiance` (already attenuated/scaled by
// the caller — see AccumulateDirectionalLight/AccumulatePointLight).
vec3 CookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 lightRadiance) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kSpecular = F;
    vec3 kDiffuse = (vec3(1.0) - kSpecular) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    return (kDiffuse * albedo / PBR_PI + specular) * lightRadiance * NdotL;
}
