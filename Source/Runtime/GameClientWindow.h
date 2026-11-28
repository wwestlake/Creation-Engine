#pragma once

#include <JuceHeader.h>

#include "Interaction/EditorInteraction.h"
#include "Frust/EngineFrustHost.h"
#include "Input/InputActionSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Render/ViewportComponent.h"
#include "Runtime/RuntimeDeveloperInspection.h"
#include "Runtime/PossessionService.h"
#include "Runtime/CameraDirector.h"
#include "engine/world.h"

#include <creation/assets/ProjectSession.h>
#include <creation/suite/SuiteSettings.h>

#include "Project/EngineGameDocument.h"

namespace ce::runtime
{

class GameClientContent final : public juce::Component, private juce::Timer
{
public:
    GameClientContent(int clientNumber, juce::ValueTree sceneState, project::GameDocumentInfo game,
                      project::SceneDocumentInfo scene, creation::assets::ProjectSession projectSession,
                      creation::suite::SuiteSettings suiteSettings, bool autoStart);
    ~GameClientContent() override;
    void Close();
    developer::RuntimeCommands& Commands() { return runtimeCommands_; }
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    developer::RuntimeOperations RuntimeOperations();
    bool Start(developer::Source source);
    bool Stop();
    void Tick();
    void PrepareScene();
    void ShowSceneStatus();
    void possessAuthoredPlayer();
    void updatePossessedPlayer(float elapsedSeconds);

    int clientNumber_;
    engine::World world_;
    // See MainComponent.h's own physicsWorld_ comment -- same lifetime
    // rule (lives alongside world_, never owned by a dockable panel).
    ce::physics::PhysicsWorld physicsWorld_;
    interaction::EditorInteraction interactions_{ world_ };
    ce::frust::EngineFrustHost frustHost_{ world_ };
    ce::input::InputActionSystem inputActionSystem_;
    RuntimeWorldRunner runtimeWorldRunner_{ world_, physicsWorld_, frustHost_ };
    PossessionService possessionService_{ world_, physicsWorld_, inputActionSystem_ };
    CameraDirector cameraDirector_;
    creation::assets::ProjectSession projectSession_;
    creation::suite::SuiteSettings suiteSettings_;
    project::GameDocumentInfo game_;
    project::SceneDocumentInfo scene_;
    juce::ValueTree initialState_;
    developer::Phase phase_ = developer::Phase::stopped;
    bool fixture_ = false;
    developer::RuntimeCommands runtimeCommands_{RuntimeOperations()};
    juce::String inputMappingError_;
    ViewportComponent viewport_;
    juce::Label hud_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameClientContent)
};

class GameClientWindow final : public juce::DocumentWindow
{
public:
    GameClientWindow(int clientNumber, juce::ValueTree sceneState, project::GameDocumentInfo game,
                     project::SceneDocumentInfo scene, creation::assets::ProjectSession projectSession,
                     creation::suite::SuiteSettings suiteSettings, bool autoStart = true);
    void closeButtonPressed() override;
    developer::RuntimeCommands& Commands();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameClientWindow)
};

} // namespace ce::runtime
