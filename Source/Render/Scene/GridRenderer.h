#pragma once

#include <JuceHeader.h>

#include "Render/GL/Buffer.h"
#include "Render/GL/VertexArray.h"

namespace ce {

// A static line grid in the XZ plane, centered at the origin — visual
// reference for the free camera (spec section 3.1's "standard scene-
// editing aids: grid, ..."). Position-only geometry (no normal/uv), so
// it's its own tiny vertex format rather than reusing the shared
// position+normal+uv Vertex struct every Mesh uses.
class GridRenderer final {
public:
    // Builds a grid spanning [-halfSize, halfSize] on both axes with a
    // line every `step` units. Needs a live GL context — call after
    // newOpenGLContextCreated, same rule as everything else that uploads
    // GPU buffers in this render layer.
    void Build(float halfSize, float step);
    void Draw();

private:
    gl::Buffer vertexBuffer_;
    gl::VertexArray vertexArray_;
    GLsizei vertexCount_ = 0;
};

} // namespace ce
