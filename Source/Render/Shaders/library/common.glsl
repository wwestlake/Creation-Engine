// Shared math helpers. No #version line here — chunks are inlined into
// an entry .vert/.frag file by ShaderComposer, which owns the version
// directive.

float Saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 Saturate(vec3 v) {
    return clamp(v, vec3(0.0), vec3(1.0));
}
