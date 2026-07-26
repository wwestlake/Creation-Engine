#pragma once

#include <vector>

#include <JuceHeader.h>

#include "Render/Scene/Vertex.h"

namespace ce {

// Generates a smooth-shaded UV sphere (unit radius, centered at origin)
// with correct per-vertex normals — a simple stand-in test mesh until
// M4 brings in real imported geometry.
void GenerateUVSphere(int rings, int segments, std::vector<Vertex>& outVertices, std::vector<GLuint>& outIndices);

// Generates a unit cube (extents -0.5..0.5, centered at origin) with
// 24 vertices (4 per face, not shared across faces) so each face gets a
// correct flat normal and its own 0..1 UV range — the standard box mesh
// layout, same one BoxTextured.gltf itself uses.
void GenerateCube(std::vector<Vertex>& outVertices, std::vector<GLuint>& outIndices);

} // namespace ce
