#pragma once

namespace ce {

// The one vertex layout every render program in this pass agrees on:
// location 0 = position, 1 = normal, 2 = uv. Real per-program vertex
// formats (e.g. skinned meshes with bone weights) are future work — for
// now a single shared layout keeps the mesh/shader contract simple.
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};

} // namespace ce
