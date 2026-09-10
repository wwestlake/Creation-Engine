#pragma once

#include <JuceHeader.h>

#include "Interaction/EditorInteraction.h"
#include "Frust/EngineFrustHost.h"
#include "Input/InputActionSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Render/ViewportComponent.h"
#include "Runtime/RuntimeWorldRunner.h"
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
    GameClientContent(int clientNumber, juce::ValueTree sceneState, juce::String gameName, juce::String sceneName,
                      juce::Array<ce::project::PlayerSlotInfo> playerSlots,
                      ce::project::GameDocumentInfo game,
                      const creation::assets::ProjectSession& projectSession,
                      const creation::suite::SuiteSettings& suiteSettings);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    void possessAuthoredPlayer();
    void updatePossessedPlayer(float elapsedSeconds);

    int clientNumber_;
    engine::World world_;
    // See MainComponent.h's own physicsWorld_ comment -- same lifetime
    // rule (lives alongside world_, never owned by a dockable panel).
    ce::physics::PhysicsWorld physicsWorld_;
    double lastFrameSeconds_ = 0.0;
    interaction::EditorInteraction interactions_{ world_ };
    ce::frust::EngineFrustHost frustHost_{ world_ };
    ce::input::InputActionSystem inputActionSystem_;
    RuntimeWorldRunner runtimeWorldRunner_{ world_, physicsWorld_, frustHost_, inputActionSystem_ };
    PossessionService possessionService_{ world_, physicsWorld_, inputActionSystem_ };
    CameraDirector cameraDirector_;
    juce::Array<ce::project::PlayerSlotInfo> playerSlots_;
    ce::project::GameDocumentInfo game_;
    juce::String inputMappingError_;
    ViewportComponent viewport_;
    juce::Label hud_;
    bool playing_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameClientContent)
};

class GameClientWindow final : public juce::DocumentWindow
{
public:
    GameClientWindow(int clientNumber, juce::ValueTree sceneState, juce::String gameName, juce::String sceneName,
                     juce::Array<ce::project::PlayerSlotInfo> playerSlots,
                     ce::project::GameDocumentInfo game,
                     const creation::assets::ProjectSession& projectSession,
                     const creation::suite::SuiteSettings& suiteSettings);
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameClientWindow)
};

} // namespace ce::runtime
