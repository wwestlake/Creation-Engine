#pragma once

namespace ce {

// The one vertex layout every render program in this pass agrees on:
// location 0 = position, 1 = normal, 2 = uv, 3 = bone indices, 4 = bone
// weights. A single shared layout (rather than a separate skinned-vertex
// type) keeps the mesh/shader contract simple — unskinned meshes
// (procedural shapes, static glTF imports) just leave boneIndices/
// boneWeights at their zero default, unread by the ordinary PBR/unlit
// shaders that never declare those attributes.
//
// boneIndices is stored as float, not int, even though it's semantically
// an index — VertexArray::SetAttribute only supports float attributes
// today (glVertexAttribPointer, not glVertexAttribIPointer). AI5's
// skinning shader casts back to int when it reads this; up to 2^24
// distinct joint indices round-trip through a float exactly, far beyond
// any plausible skeleton size, so nothing is lost in practice.
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float boneIndices[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float boneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

} // namespace ce
