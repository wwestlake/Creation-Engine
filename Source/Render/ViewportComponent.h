#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>

#include <JuceHeader.h>

#include "engine/world.h"
#include "Render/Scene/Camera.h"
#include "Render/Scene/FreeCamera.h"
#include "Render/Scene/GridRenderer.h"
#include "Render/Scene/Light.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/GL/Buffer.h"
#include "Render/GL/VertexArray.h"
#include "Render/Shaders/ShaderComposer.h"
#include "Scene/AssetCatalog.h"
#include "Interaction/EditorInteraction.h"
#include "VR/OpenXRProvider.h"

namespace creation::assets { class ProjectSession; }
namespace creation::suite { struct SuiteSettings; }

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
// separate from the message thread mouseDown/mouseDrag/mouseUp arrive on.
// Two locks, two different things:
//   - stateLock_ (juce::CriticalSection) guards sunLight_/pointLights_/
//     lastCameraPosition_/lastCameraForward_ — plain members, nothing to
//     do with the ECS registry.
//   - world_.RegistryMutex() (std::mutex, owned by World itself so every
//     class touching the shared World uses the same lock) guards every
//     access to world_.Registry() — entities, Transforms, and the Mesh/
//     Material each MeshRenderer references. As of SC3, HierarchyPanel
//     reads the registry from the message thread while this class reads
//     and mutates it from the render thread every frame; that's a real,
//     exercised race without the lock, not a hypothetical.
// Either way: copy in, copy out, never hand back a live reference the UI
// thread could hold onto while the render thread is mid-frame.
//
// Gizmo picking/dragging deliberately does NOT follow the "share via
// RegistryMutex" rule above, for the same reason FreeCamera's WASD/look
// input doesn't: RegistryMutex is also locked by this same render thread
// every single frame just to draw the gizmo (updateDesktopTransformGizmo,
// below), so a continuous stream of message-thread lock acquisitions
// during a drag (one per mouse-move, far more than 60/sec) fights the
// render thread for that same lock and reliably hangs the app. Camera
// look/fly never had this problem because it never crosses threads at
// all -- FreeCamera reads raw input state (atomics) and does all its
// actual position math from inside Update(), called from here on the
// render thread. Gizmo interaction follows the same shape: mouseDown/
// mouseDrag/mouseUp (below) only record where the mouse is and whether
// the button is down, as atomics; updateDesktopTransformDrag(), called
// once per frame from renderOpenGL() alongside updateDesktopTransformGizmo(),
// does the actual ray cast, hit test, and EditorInteraction calls -- all
// on the render thread, so RegistryMutex is never contended by two
// threads during a drag, only ever touched by the one that already owns
// it every frame regardless.
class ViewportComponent final : public juce::Component,
                                 private juce::OpenGLRenderer {
public:
    // renderSurfaceHost is where the OpenGLContext actually attaches -- an
    // always-alive component owned outside the dock tree (see
    // MainComponent::viewportRenderHost_), so tab-switching this panel
    // never tears the context down and re-creates it (a real, reproduced
    // crash from stale GPU handles -- see docs/... AGENTS.md Do It Right
    // Rule for why this isn't a smaller per-panel patch instead). This
    // component itself keeps receiving mouse/keyboard input as the actual
    // dock panel; only where the rendered pixels land moves.
    explicit ViewportComponent(engine::World& world, interaction::EditorInteraction& interactions,
                                juce::Component& renderSurfaceHost);
    ~ViewportComponent() override;

    void paint(juce::Graphics&) override {}
    void resized() override {}
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

    DirectionalLight GetSunLight() const;
    void SetSunLight(const DirectionalLight& light);

    int GetPointLightCount() const;
    PointLight GetPointLight(int index) const;
    void SetPointLight(int index, const PointLight& light);
    void AddPointLight();
    void RemovePointLight(int index);

    void EnableFirstPersonMode() { freeCamera_.EnableFirstPersonMode(); }

    // Catalog access for SC5's "+ Add" menu (HierarchyPanel) to list/look
    // up placeable assets, and for Source/Import's importers to register
    // newly-imported ones. Safe to call from any thread without an
    // external lock -- AssetCatalog guards its own state internally (see
    // its own header comment) precisely because it's no longer just
    // populated once at startup.
    const scene::AssetCatalog& Catalog() const { return assetCatalog_; }
    scene::AssetCatalog& Catalog() { return assetCatalog_; }

    // A point `distance` units in front of the free camera, for SC5's
    // "place a new entity where I'm looking" — reads the snapshot taken
    // under stateLock_ each frame (see stateLock_ doc above), not
    // freeCamera_ directly, since freeCamera_'s position is only safe to
    // touch from the render thread.
    juce::Vector3D<float> SpawnPosition(float distance = 4.0f) const;

    // Runs work that needs a current GL context (Mesh::Upload,
    // Texture2D::Load*, ...) on the render thread, regardless of which
    // thread calls RunOnGLThread itself. For the asset importers
    // (Source/Import): a drag-and-drop lands on the message thread, but
    // building GPU resources for the imported asset can't happen there.
    void RunOnGLThread(std::function<void()> work, bool blockUntilFinished);

    // Resolves durable project asset references into this viewport's GPU
    // cache after a game scene opens. It does not author or rewrite content.
    void ResolveProjectAssets(const creation::assets::ProjectSession& session,
                              const creation::suite::SuiteSettings& settings);

private:
    enum class TranslationHandle { none, xAxis, yAxis, zAxis, xyPlane, xzPlane, yzPlane, free };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void updateVREditorRig(float deltaSeconds);
    void updateVRInteraction();
    void updateDesktopTransformDrag();
    void updateDesktopTransformGizmo();
    void uploadVRWands();
    void uploadVRTransformGizmo();
    void uploadVREditorCart();
    [[nodiscard]] bool desktopRay(const juce::Point<float>& screenPosition,
                                  juce::Vector3D<float>& origin,
                                  juce::Vector3D<float>& direction) const;
    [[nodiscard]] entt::entity desktopPick(const juce::Vector3D<float>& origin,
                                           const juce::Vector3D<float>& direction,
                                           float& distance) const;
    [[nodiscard]] TranslationHandle desktopGizmoHandle(const juce::Vector3D<float>& origin,
                                                        const juce::Vector3D<float>& direction) const;
    [[nodiscard]] interaction::TranslationConstraint constraintFor(TranslationHandle handle) const;
    // xAxis/yAxis/zAxis -> 0/1/2, free -> -1 (uniform scale / free-move),
    // anything else (the position-only plane handles) -> -2, not
    // meaningful outside position mode.
    [[nodiscard]] static int HandleAxisIndex(TranslationHandle handle);

    engine::World& world_;
    interaction::EditorInteraction& interactions_;
    juce::Component& renderSurfaceHost_;

    juce::OpenGLContext openGLContext_;
    std::unique_ptr<vr::OpenXRProvider> openXRProvider_;
    engine::vr::FrameState vrFrame_{};
    // The OpenXR eye pose already contains physical headset height.  This
    // rig is the world-space floor anchor, so adding another 1.6 m here
    // would make the editor float above its own scene.
    juce::Vector3D<float> vrRigPosition_{ 0.0f, 0.0f, 5.0f };
    float vrRigYaw_ = 0.0f;
    float vrRigPitch_ = 0.0f;
    struct VRWandRay {
        juce::Vector3D<float> start, end, surfaceNormal;
        bool active = false;
        bool hit = false;
        entt::entity entity = entt::null;
    };
    struct VRTransformGizmo {
        entt::entity entity = entt::null;
        juce::Vector3D<float> center;
        float scale = 1.0f;
        TranslationHandle hovered = TranslationHandle::none;
        // World unit axes by default; recomputed each frame from the
        // selected entity's own rotation when GizmoMode::position and
        // TransformSpace::local are both active -- position mode only,
        // see EditorInteraction.h's beginTranslation comment for why
        // scale/rotate aren't included here yet.
        std::array<juce::Vector3D<float>, 3> positionAxes{{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }};
    };
    std::array<VRWandRay, 2> vrWands_{};
    std::array<TranslationHandle, 2> vrHoveredHandles_{};
    VRTransformGizmo vrTransformGizmo_{};
    juce::Vector3D<float> vrHoverCenter_{};
    juce::Vector3D<float> vrHoverNormal_{ 0.0f, 0.0f, 1.0f };
    float vrHoverRadius_ = 0.0f;
    bool vrHoverActive_ = false;
    int vrTranslationController_ = -1;
    std::array<bool, 2> vrGripWasPressed_{};
    std::array<bool, 2> vrSelectWasPressed_{};
    gl::Buffer vrWandVertexBuffer_;
    gl::VertexArray vrWandVertexArray_;
    GLsizei vrWandVertexCount_ = 0;
    gl::Buffer vrGizmoVertexBuffer_;
    gl::VertexArray vrGizmoVertexArray_;
    GLsizei vrGizmoVertexCount_ = 0;
    gl::Buffer vrCartVertexBuffer_;
    gl::VertexArray vrCartVertexArray_;
    GLsizei vrCartDeckVertexCount_ = 0;
    GLsizei vrCartVertexCount_ = 0;
    std::unique_ptr<ShaderComposer> shaderComposer_;
    scene::AssetCatalog assetCatalog_;

    // Written from mouseDown/mouseDrag/mouseUp (message thread), read once
    // per frame from updateDesktopTransformDrag() (render thread) -- see
    // the class comment above for why this is atomics-only, no mutex.
    std::atomic<bool> desktopMouseButtonDown_{ false };
    std::atomic<float> desktopMouseX_{ 0.0f };
    std::atomic<float> desktopMouseY_{ 0.0f };

    // Render-thread-only state below: never touched from mouseDown/
    // mouseDrag/mouseUp, only from updateDesktopTransformDrag().
    bool desktopMouseWasDown_ = false;
    bool desktopTransformDrag_ = false;
    float desktopDragDistance_ = 0.0f;
    int desktopRotationAxisIndex_ = -1;

    mutable juce::CriticalSection stateLock_;
    DirectionalLight sunLight_;
    std::vector<PointLight> pointLights_{ PointLight{} };
    juce::Vector3D<float> lastCameraPosition_;
    juce::Vector3D<float> lastCameraForward_{ 0.0f, 0.0f, -1.0f };

    Camera camera_;
    FreeCamera freeCamera_;
    GridRenderer gridRenderer_;
    juce::OpenGLShaderProgram* gridProgram_ = nullptr;
    double lastFrameTimeSeconds_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewportComponent)
};

} // namespace ce
