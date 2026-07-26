#pragma once

#include <JuceHeader.h>

#include "Render/Scene/Camera.h"
#include "Render/Scene/Mesh.h"
#include "Render/Shaders/ShaderComposer.h"

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
// Current state (M2 of the render pipeline plan): a procedural sphere
// shaded by a Cook-Torrance PBR program assembled at runtime by
// ShaderComposer from the reusable GLSL chunk library
// (Source/Render/Shaders/library/), lit by one directional and one point
// light. An "unlit" normal-visualization program is also composed and
// cached, proving the composer handles more than one program/variant.
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
    std::unique_ptr<ShaderComposer> shaderComposer_;
    juce::OpenGLShaderProgram* pbrProgram_ = nullptr;
    juce::OpenGLShaderProgram* unlitProgram_ = nullptr;
    Mesh sphereMesh_;
    Camera camera_;
    double startTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
