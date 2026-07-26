#pragma once

#include <vector>

#include <JuceHeader.h>

#include "Render/GL/Buffer.h"
#include "Render/GL/VertexArray.h"
#include "Render/Scene/Vertex.h"

namespace ce {

// GPU-resident vertex/index data for one drawable mesh, using the shared
// Vertex layout. Uploaded once from CPU-side data (procedural generation
// today, glTF import from M4 onward) and drawn every frame after.
class Mesh final {
public:
    void Upload(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices);
    void Draw();

private:
    gl::Buffer vertexBuffer_;
    gl::Buffer indexBuffer_;
    gl::VertexArray vertexArray_;
    GLsizei indexCount_ = 0;
};

} // namespace ce
