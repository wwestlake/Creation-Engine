#pragma once

#include <memory>
#include <vector>

#include <JuceHeader.h>

#include "assets/VirtualFileSystem.h"
#include "Render/GL/Texture2D.h"
#include "Render/Scene/Camera.h"
#include "Render/Scene/Light.h"
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
// Current state (VFS follow-on to M5): the demo glTF asset and its base
// color texture load through a mounted .zip archive (assets/packages/
// base.zip) via VirtualFileSystem (AssetSystem/) instead of loose disk
// files — LoadGltfFromVfs + Texture2D::LoadFromMemory, fully in-memory.
// Lit by an editable light list — one directional light plus up to
// kMaxPointLights point lights — surfaced through LightPanel
// (Source/Views/LightPanel.h) in MainComponent's inspector dock.
//
// juce::OpenGLContext runs renderOpenGL() on its own background thread,
// separate from the message thread these editing methods are called
// from. Every method below that touches light or material-tweakable
// state takes stateLock_ for exactly that reason — copy in, copy out,
// never hand back a live reference the UI thread could hold onto while
// the render thread is mid-frame.
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

    DirectionalLight GetSunLight() const;
    void SetSunLight(const DirectionalLight& light);

    int GetPointLightCount() const;
    PointLight GetPointLight(int index) const;
    void SetPointLight(int index, const PointLight& light);
    void AddPointLight();
    void RemovePointLight(int index);

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext openGLContext_;
    std::unique_ptr<ShaderComposer> shaderComposer_;
    assets::VirtualFileSystem vfs_;

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    std::vector<int> primitiveMaterialIndex_; // parallel to meshes_
    std::vector<std::unique_ptr<gl::Texture2D>> loadedTextures_;

    mutable juce::CriticalSection stateLock_;
    DirectionalLight sunLight_;
    std::vector<PointLight> pointLights_{ PointLight{} };

    Camera camera_;
    double startTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
