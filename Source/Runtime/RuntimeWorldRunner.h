#pragma once

#include <functional>

#include <juce_data_structures/juce_data_structures.h>

#include "Project/EngineGameDocument.h"

namespace ce::physics { class PhysicsWorld; }
namespace ce::frust { class EngineFrustHost; class PodCatalog; }

namespace ce::runtime
{

// A game and its content source belong to one runtime. Disk paths and the
// rendering/input devices never enter the simulation loop.
struct RuntimeGameSource
{
    project::GameDocumentInfo game;
    std::function<bool(const project::SceneDocumentInfo&, juce::ValueTree&, juce::String&)> readScene;
    std::function<bool(frust::PodCatalog&, juce::String&)> readPods;
    std::function<bool(const std::string&)> assetExists;
    std::function<bool(const std::string&, std::string&)> readModule;
    std::vector<std::string> comboNames;
};

RuntimeGameSource FromProject(creation::assets::ProjectSession session, project::GameDocumentInfo game);

class RuntimeWorldRunner final
{
public:
    static constexpr float defaultStepSeconds = 1.0f / 30.0f;

    struct Hooks
    {
        std::function<void(float)> afterPhysics;
        std::function<void()> beforeSceneChange;
        std::function<void()> afterSceneChange;
        std::function<bool()> canChangeScene;
    };

    RuntimeWorldRunner(engine::World& world, physics::PhysicsWorld& physics, frust::EngineFrustHost& frust,
                       float stepSeconds = defaultStepSeconds);
    ~RuntimeWorldRunner();

    RuntimeWorldRunner(const RuntimeWorldRunner&) = delete;
    RuntimeWorldRunner& operator=(const RuntimeWorldRunner&) = delete;

    bool Start(RuntimeGameSource source, project::SceneDocumentInfo scene, juce::ValueTree initialState = {},
               const std::function<bool(juce::String&)>& prepareHost = {}, engine::Tick initialTick = 0);
    void Stop();

    // Called once for each scheduled tick or explicit step. All operations
    // run on the runtime owner's thread. Device acquisition is host-owned.
    void Tick(const Hooks& hooks = {});

    [[nodiscard]] const juce::String& ContextId() const noexcept { return contextId_; }
    [[nodiscard]] const project::GameDocumentInfo& Game() const noexcept { return source_.game; }
    [[nodiscard]] const project::SceneDocumentInfo& Scene() const noexcept { return scene_; }
    [[nodiscard]] const juce::String& LastError() const noexcept { return error_; }
    [[nodiscard]] bool IsPlaying() const noexcept { return playing_; }
    [[nodiscard]] float StepSeconds() const noexcept { return stepSeconds_; }

private:
    bool LoadBehaviors();
    void ApplyTransition(const Hooks& hooks);

    engine::World& world_;
    physics::PhysicsWorld& physics_;
    frust::EngineFrustHost& frust_;
    const float stepSeconds_;
    const juce::String contextId_ = juce::Uuid().toString();
    RuntimeGameSource source_;
    project::SceneDocumentInfo scene_;
    juce::String pendingScene_;
    juce::String error_;
    bool playing_ = false;
};

} // namespace ce::runtime
