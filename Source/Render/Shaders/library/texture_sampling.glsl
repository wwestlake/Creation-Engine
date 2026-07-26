// Basic texture-sampling utilities. Not yet wired into a live program —
// M3 adds the Texture2D GL wrapper and Material system that will bind a
// real sampler here. Kept as its own chunk now so the material graph
// (future work) and hand-written shaders share the same sampling code.

vec4 SampleAlbedo(sampler2D albedoTexture, vec2 uv) {
    return texture(albedoTexture, uv);
}

vec3 SampleNormalMap(sampler2D normalTexture, vec2 uv) {
    vec3 tangentSpaceNormal = texture(normalTexture, uv).xyz * 2.0 - 1.0;
    return normalize(tangentSpaceNormal);
}
