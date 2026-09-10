#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>

namespace ce {
class Mesh;
class Material;
}

namespace ce::render {

// One entity's fully-resolved, render-ready state, copied out of the live
// World registry once per update tick (ViewportComponent::PublishFrameSnapshot,
// called from MainComponent::timerCallback() every tick -- whether or not
// isPlaying_, since edit-mode changes need to reach the viewport too) so
// renderOpenGL()'s draw pass never touches world_.Registry() or
// world_.RegistryMutex() at all. Generalizes the double-buffer copy-out
// PhysicsRenderState already proves out for physics alone
// (Source/Physics/PhysicsComponents.h) to the whole draw pass. Engine Loop
// Decoupling plan, Phase 2.
//
// Desktop gizmo picking/dragging and mouse-click selection remain editor
// interaction state rather than renderable state. They execute on the JUCE
// message thread and publish only the tiny gizmo draw description the render
// thread needs; the renderer does not read world_.Registry() for desktop
// interaction. This snapshot is scoped to the continuous draw pass only --
// mesh rendering, materials, skeletal animation -- which is the actual
// "render and update everything
// at once" problem reported: physics/possession/animation and the render
// loop fighting over one shared, live registry every single frame.
struct RenderableEntity {
    juce::Matrix3D<float> worldTransform;
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    // Selection highlight and any runtime engine::Tint are both already
    // resolved into this single color at snapshot-build time -- render
    // itself never needs to know which entity is selected.
    juce::Vector3D<float> tint{ 1.0f, 1.0f, 1.0f };
    std::unordered_map<std::string, float> parameterScalarOverrides;
    std::unordered_map<std::string, juce::Vector3D<float>> parameterColorOverrides;
    // Empty for a non-skinned entity. Already sampled/blended/skinned this
    // tick (Animator playback advances once per update tick here, at
    // snapshot-build time -- not once per render frame as before,
    // matching physics' own fixed-tick-not-render-tick cadence).
    std::vector<juce::Matrix3D<float>> boneMatrices;
};

struct FrameSnapshot {
    std::vector<RenderableEntity> renderables;
};

} // namespace ce::render
