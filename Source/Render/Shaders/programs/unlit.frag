#version 410 core

// Normal-visualization debug shading: no lighting math, just maps the
// world-space normal into an RGB color. Useful for sanity-checking mesh
// normals independent of any material/lighting bugs.

in vec3 vNormal;
out vec4 fragColor;

void main() {
    fragColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
