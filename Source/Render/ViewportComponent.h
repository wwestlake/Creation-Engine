#pragma once

#include <JuceHeader.h>

#include "Render/GL/Buffer.h"
#include "Render/GL/VertexArray.h"
#include "Render/Scene/Camera.h"

namespace ce {

// Owns the OpenGL context bound to the 3D viewport. Per the capabilities
// spec (section 3.1), this renders the actual game scene at interactive
// framerates using the same rendering path the shipped game uses — this
// class is the seed of that path, not a simplified editor-only proxy.
//
// Tooling panels (inspectors, node graph editors) are separate JUCE
// components composited in the same window/process as this viewport
// (section 3.4) — see MainComponent for how they're arranged around it.
//
// Current state (M1 of the render pipeline plan): a hardcoded, vertex-
// colored cube proving the OpenGL 4.1 core context, VBO/EBO/VAO plumbing,
// and shader compile/link path all work end to end. The shader source
// here is a hand-written placeholder — M2 replaces it with a program
// assembled by the shader composer from the reusable GLSL library.
class ViewportComponent final : public juce::Component,
                                 private juce::OpenGLRenderer {
public:
    ViewportComponent();
    ~ViewportComponent() override;

    void paint(juce::Graphics&) override {}
    void resized() override {}

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext openGLContext_;
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
    gl::Buffer vertexBuffer_;
    gl::Buffer indexBuffer_;
    gl::VertexArray vertexArray_;
    Camera camera_;
    double startTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
