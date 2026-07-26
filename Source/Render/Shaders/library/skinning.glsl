// Linear-blend (LBS) skinning: blends up to 4 bone matrices per vertex by
// aBoneWeights, matching the JOINTS_0/WEIGHTS_0 data glTF skins provide
// and Vertex.h's boneIndices/boneWeights already carry unconditionally.
// Only compiled in when the owning Material set USE_SKINNING (see
// Material::Resolve) -- an ordinary unskinned mesh's shader program never
// even declares uBoneMatrices.
#ifdef USE_SKINNING

// Matches ce::kMaxBones (Source/Render/Scene/Animation.h) -- the CPU-side
// bone matrix palette is capped at the same size as this array.
#define CE_MAX_BONES 64
uniform mat4 uBoneMatrices[CE_MAX_BONES];

mat4 SkinMatrix(vec4 boneIndices, vec4 boneWeights) {
    mat4 skin = mat4(0.0);
    skin += boneWeights.x * uBoneMatrices[int(boneIndices.x)];
    skin += boneWeights.y * uBoneMatrices[int(boneIndices.y)];
    skin += boneWeights.z * uBoneMatrices[int(boneIndices.z)];
    skin += boneWeights.w * uBoneMatrices[int(boneIndices.w)];
    return skin;
}

#endif
