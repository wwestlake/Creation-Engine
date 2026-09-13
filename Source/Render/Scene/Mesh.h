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
    struct Bounds {
        juce::Vector3D<float> minimum{};
        juce::Vector3D<float> maximum{};
    };

    void Upload(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices);
    void Draw();

    // CPU-side bounds are retained after upload so placement systems can
    // anchor a visual asset to a physical convention without rereading it.
    [[nodiscard]] const Bounds& bounds() const noexcept { return bounds_; }

private:
    gl::Buffer vertexBuffer_;
    gl::Buffer indexBuffer_;
    gl::VertexArray vertexArray_;
    GLsizei indexCount_ = 0;
    Bounds bounds_{};
};

} // namespace ce
