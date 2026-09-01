#version 410 core

uniform vec3 uColor;
// Only meaningful when the draw call that issues it also enables
// GL_BLEND (see ViewportComponent's plane-handle fill draw) -- every
// other user of this shader leaves blending off, so this value is
// simply never read for those, whatever it happens to hold.
uniform float uAlpha = 1.0;

out vec4 fragColor;

void main() {
    fragColor = vec4(uColor, uAlpha);
}
