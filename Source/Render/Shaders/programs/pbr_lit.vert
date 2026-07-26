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

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 localPosition = vec4(aPosition, 1.0);
    vec3 localNormal = aNormal;

#ifdef USE_SKINNING
    mat4 skin = SkinMatrix(aBoneIndices, aBoneWeights);
    localPosition = skin * localPosition;
    localNormal = mat3(skin) * localNormal;
#endif

    vec4 worldPos = uModel * localPosition;
    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * localNormal);
    vUV = aUV;
    gl_Position = uProjection * uView * worldPos;
}
