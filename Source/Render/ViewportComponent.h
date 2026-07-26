#pragma once

#include <JuceHeader.h>

#include "Render/GL/Texture2D.h"
#include "Render/Scene/Camera.h"
#include "Render/Scene/Material.h"
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
// Current state (M3 of the render pipeline plan): a procedural sphere
// shaded by a Material (Source/Render/Scene/Material.h) wrapping a
// Cook-Torrance PBR program assembled by ShaderComposer, textured with a
// generated checker pattern via the USE_ALBEDO_TEXTURE shader variant.
// Roughness/metallic are live-tweakable from MainComponent's inspector.
class ViewportComponent final : public juce::Component,
                                 private juce::OpenGLRenderer {
public:
    ViewportComponent();
    ~ViewportComponent() override;

    void paint(juce::Graphics&) override {}
    void resized() override {}

    void SetRoughness(float value) { material_.roughness = value; }
    void SetMetallic(float value) { material_.metallic = value; }
    float Roughness() const { return material_.roughness; }
    float Metallic() const { return material_.metallic; }

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext openGLContext_;
    std::unique_ptr<ShaderComposer> shaderComposer_;
    Material material_;
    gl::Texture2D checkerTexture_;
    Mesh sphereMesh_;
    Camera camera_;
    double startTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
