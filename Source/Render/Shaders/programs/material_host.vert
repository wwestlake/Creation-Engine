#version 410 core
#include "library/skinning.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
#ifdef USE_SKINNING
layout(location = 3) in vec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;
#endif

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform vec3 uCameraPos;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

// Filled with the SAME compiled text material_host.frag receives (see
// ShaderComposer::GetProgram splicing the identical materialSource into
// both stages, and Material::Resolve() requesting this file + it
// together) -- this host only calls EvaluateWorldPositionOffset out of
// it, ignoring the EvaluateMaterial function sitting alongside it in the
// same compiled unit, harmlessly unused here. worldPosition/worldNormal/
// cameraVector/time below are all computed the same way pbr_lit.vert
// already computes vWorldPos/vNormal -- genuinely valid per-vertex
// quantities, not fragment-only approximations.
//$MATERIAL_SOURCE$

void main() {
    vec4 localPosition = vec4(aPosition, 1.0);
    vec3 localNormal = aNormal;

#ifdef USE_SKINNING
    mat4 skin = SkinMatrix(aBoneIndices, aBoneWeights);
    localPosition = skin * localPosition;
    localNormal = mat3(skin) * localNormal;
#endif

    vec4 worldPos = uModel * localPosition;
    vec3 worldNormal = normalize(uNormalMatrix * localNormal);
    vec3 cameraVector = normalize(uCameraPos - worldPos.xyz);

    // Normal is intentionally left as the unperturbed geometric normal
    // even when an offset displaces the vertex -- correctly re-deriving
    // it (analytic surface derivatives, or resampling neighboring
    // vertices) is real additional work, deferred rather than faked here.
    worldPos.xyz += EvaluateWorldPositionOffset(localPosition.xyz, worldPos.xyz, worldNormal, cameraVector, uTime, aUV);

    vWorldPos = worldPos.xyz;
    vNormal = worldNormal;
    vUV = aUV;
    gl_Position = uProjection * uView * worldPos;
}
