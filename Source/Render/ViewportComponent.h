#pragma once

#include <memory>
#include <vector>

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
// Current state (M4 of the render pipeline plan): renders a real glTF
// 2.0 asset (Source/Render/Import/GltfLoader) — geometry, PBR material
// factors, and base color texture all pulled from the file, drawn
// through the same Mesh/Material/ShaderComposer pipeline M2/M3 built.
// Node-hierarchy transforms aren't applied yet (each primitive is drawn
// in its own local space under one shared spin animation) — full scene
// graph import is future work.
class ViewportComponent final : public juce::Component,
                                 private juce::OpenGLRenderer {
public:
    ViewportComponent();
    ~ViewportComponent() override;

    void paint(juce::Graphics&) override {}
    void resized() override {}

    void SetRoughness(float value);
    void SetMetallic(float value);
    float Roughness() const;
    float Metallic() const;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext openGLContext_;
    std::unique_ptr<ShaderComposer> shaderComposer_;

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    std::vector<int> primitiveMaterialIndex_; // parallel to meshes_
    std::vector<std::unique_ptr<gl::Texture2D>> loadedTextures_;

    Camera camera_;
    double startTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
