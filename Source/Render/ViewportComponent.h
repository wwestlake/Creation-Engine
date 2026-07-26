#pragma once

#include <limits>
#include <memory>

#include <JuceHeader.h>

#include "assets/VirtualFileSystem.h"
#include "engine/world.h"
#include "Render/Scene/Camera.h"
#include "Render/Scene/FreeCamera.h"
#include "Render/Scene/GridRenderer.h"
#include "Render/Scene/Light.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/Shaders/ShaderComposer.h"
#include "Scene/AssetCatalog.h"

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
// Current state (SC2 of the scene-composition plan): a free-fly camera
// (Source/Render/Scene/FreeCamera.h — hold right mouse + WASD/Q/E) over a
// reference ground grid (GridRenderer), replacing the earlier fixed
// SetLookAt. Draws every entity
// in the shared ce::engine::World that has both a scene::Transform and a
// scene::MeshRenderer component, instead of a fixed hardcoded model —
// per spec section 1, placed objects are real entities in the same World
// the game will simulate against, not a separate editor-only scene list.
// AssetCatalog (Source/Scene/AssetCatalog.h) owns the GPU-resident
// Mesh/Material/Texture2D assets those components reference.
//
// juce::OpenGLContext runs renderOpenGL() on its own background thread,
// separate from the message thread these editing methods are called
// from. Two locks, two different things:
//   - stateLock_ (juce::CriticalSection) guards sunLight_/pointLights_ —
//     plain members, nothing to do with the ECS registry.
//   - world_.RegistryMutex() (std::mutex, owned by World itself so every
//     class touching the shared World uses the same lock) guards every
//     access to world_.Registry() — entities, Transforms, and the Mesh/
//     Material each MeshRenderer references. As of SC3, HierarchyPanel
//     reads the registry from the message thread while this class reads
//     and mutates it from the render thread every frame; that's a real,
//     exercised race without the lock, not a hypothetical.
// Either way: copy in, copy out, never hand back a live reference the UI
// thread could hold onto while the render thread is mid-frame.
class ViewportComponent final : public juce::Component,
                                 private juce::OpenGLRenderer {
public:
    explicit ViewportComponent(engine::World& world);
    ~ViewportComponent() override;

    void paint(juce::Graphics&) override {}
    void resized() override {}
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

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

    // Called on Stop (see MainComponent's transport wiring) — resets the
    // seeded demo entity's transform, giving Stop a real, visible effect
    // instead of just freezing the tick counter.
    void ResetDemoEntityTransform();

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void SeedDemoScene();

    engine::World& world_;

    juce::OpenGLContext openGLContext_;
    std::unique_ptr<ShaderComposer> shaderComposer_;
    assets::VirtualFileSystem vfs_;
    scene::AssetCatalog assetCatalog_;
    bool hasSeededDemoScene_ = false;
    entt::entity demoEntity_ = entt::null;

    // Tracks the World tick the demo spin was last applied for, so the
    // spin write only happens when the tick actually advances (i.e. while
    // playing) rather than unconditionally every rendered frame. Without
    // this, the render thread would stomp any inspector edit to the demo
    // entity's rotation back to the spin value on the very next frame,
    // even while paused/editing — max() as the initial sentinel so the
    // very first frame (real tick 0) still counts as "changed."
    engine::Tick lastSpinTick_ = std::numeric_limits<engine::Tick>::max();

    mutable juce::CriticalSection stateLock_;
    DirectionalLight sunLight_;
    std::vector<PointLight> pointLights_{ PointLight{} };

    Camera camera_;
    FreeCamera freeCamera_;
    GridRenderer gridRenderer_;
    juce::OpenGLShaderProgram* gridProgram_ = nullptr;
    double lastFrameTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
