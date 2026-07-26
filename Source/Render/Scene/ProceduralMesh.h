#pragma once

#include <vector>

#include <JuceHeader.h>

#include "Render/Scene/Vertex.h"

namespace ce {

// Generates a smooth-shaded UV sphere (unit radius, centered at origin)
// with correct per-vertex normals — a simple stand-in test mesh until
// M4 brings in real imported geometry.
void GenerateUVSphere(int rings, int segments, std::vector<Vertex>& outVertices, std::vector<GLuint>& outIndices);

} // namespace ce
