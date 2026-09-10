#include "GameClientWindow.h"

#include <creation/ui/CreationSuiteLogos.h>

#include "Scene/EngineSceneSerializer.h"
#include "Scene/Components.h"

#include <cmath>
#include <mutex>

namespace ce::runtime
{

GameClientContent::GameClientContent(int clientNumber, juce::ValueTree sceneState,
                                     juce::String gameName, juce::String sceneName,
                                     juce::Array<ce::project::PlayerSlotInfo> playerSlots,
                                     ce::project::GameDocumentInfo game,
                                     const creation::assets::ProjectSession& projectSession,
                                     const creation::suite::SuiteSettings& suiteSettings)
    // Unlike the editor's docked Scene Viewport, this window is a standalone
    // top-level window with no tab-hide/show to tear its GL context down --
    // self-attaching remains correct here (see ViewportComponent's
    // renderSurfaceHost_ comment for why the editor's viewport needs an
    // external host instead).
    : clientNumber_(clientNumber), playerSlots_(std::move(playerSlots)), game_(std::move(game)), world_(), viewport_(world_, interactions_, viewport_)
{
    physicsWorld_.AttachToWorld(world_);
    frustHost_.setInputActionSystem(&inputActionSystem_);
    frustHost_.setPhysicsWorld(&physicsWorld_);
    juce::String inputError;
    inputActionSystem_.LoadForGame(projectSession, game_, inputError, "gameplay");
    inputMappingError_ = inputError;
    // A run client owns an isolated World, but it begins with the exact
    // authored scene selected in the editor rather than a blank test world.
    ce::scene::EngineSceneSerializer::restoreScene(world_, sceneState);
    addAndMakeVisible(viewport_);
    viewport_.ResolveProjectAssets(projectSession, suiteSettings);
    viewport_.EnableFirstPersonMode();
    possessAuthoredPlayer();
    hud_.setText(inputMappingError_.isEmpty()
                     ? "CLIENT " + juce::String(clientNumber_) + "  |  " + gameName + " / " + sceneName
                     : "INPUT MAPPING ERROR: " + inputMappingError_,
                 juce::dontSendNotification);
    hud_.setColour(juce::Label::textColourId, juce::Colours::white);
    hud_.setColour(juce::Label::backgroundColourId, juce::Colour(0xaa10141a));
    hud_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hud_);
    runtimeWorldRunner_.BeginPlay();
    startTimerHz(30);
}

void GameClientContent::resized()
{
    viewport_.setBounds(getLocalBounds());
    hud_.setBounds(12, 12, 250, 28);
}

void GameClientContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0b0e12));
}

void GameClientContent::timerCallback()
{
    if (playing_)
    {
        const double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const float elapsedSeconds = lastFrameSeconds_ > 0.0
            ? static_cast<float>(nowSeconds - lastFrameSeconds_) : 0.0f;
        lastFrameSeconds_ = nowSeconds;
        runtimeWorldRunner_.RunFrame(elapsedSeconds, {
            [this](float dt) { updatePossessedPlayer(dt); }
        });
    }
    viewport_.PublishFrameSnapshot();
    viewport_.repaint();
}

void GameClientContent::possessAuthoredPlayer()
{
    const auto slotId = playerSlots_.isEmpty() ? juce::String("player-1") : playerSlots_.getFirst().id;
    PossessionRequest request;
    request.controllerId = "client-" + juce::String(clientNumber_);
    request.inputContext = "gameplay";
    request.authority = "local";
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        const auto spawns = registry.view<ce::scene::PossessionSpawn, ce::engine::Transform>();
        entt::entity spawnEntity = entt::null;
        for (const auto entity : spawns)
        {
            if (spawns.get<ce::scene::PossessionSpawn>(entity).playerSlotId == slotId)
            {
                spawnEntity = entity;
                break;
            }
        }
        if (spawnEntity == entt::null)
            return;

        entt::entity characterEntity = entt::null;
        if (registry.all_of<ce::scene::CharacterInstanceRef>(spawnEntity))
            characterEntity = spawnEntity;
        else
        {
            const auto instances = registry.view<ce::scene::CharacterInstanceRef, ce::engine::Transform>();
            for (const auto entity : instances)
            {
                const auto& instance = instances.get<ce::scene::CharacterInstanceRef>(entity);
                if (instance.state.getWithDefault("playerSlotId", {}).toString() == slotId)
                {
                    characterEntity = entity;
                    break;
                }
            }
        }
        if (characterEntity == entt::null)
            return;

        const auto spawnPosition = spawns.get<ce::engine::Transform>(spawnEntity).position;
        auto& characterTransform = registry.get<ce::engine::Transform>(characterEntity);
        characterTransform.position = spawnPosition;
        const auto& instance = registry.get<ce::scene::CharacterInstanceRef>(characterEntity);
        request.subjectEntityId = static_cast<std::int64_t>(entt::to_integral(characterEntity));
        request.subjectInstanceId = instance.instanceId;
        request.capsuleRadiusMeters = static_cast<float>(instance.state.getWithDefault("capsuleRadiusMeters", 0.3));
        request.capsuleHalfHeightMeters = static_cast<float>(instance.state.getWithDefault("capsuleHalfHeightMeters", 0.9));
    }

    juce::String error;
    if (!possessionService_.possessCharacter(request, error))
        return;

    viewport_.EnterPossessedMode();
    cameraDirector_.attach(request.subjectEntityId);
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        const auto& transform = world_.Registry().get<ce::engine::Transform>(static_cast<entt::entity>(request.subjectEntityId));
        viewport_.SetPossessedFeetPosition({ transform.position.x, transform.position.y, transform.position.z });
    }
}

void GameClientContent::updatePossessedPlayer(float elapsedSeconds)
{
    if (!possessionService_.isPossessing())
        return;

    if (inputActionSystem_.WasActionPressed("CameraMode"))
        cameraDirector_.cycleMode();

    const auto forward = viewport_.CameraForward();
    const float yawRadians = std::atan2(forward.x, -forward.z);
    possessionService_.tick(yawRadians, elapsedSeconds);

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    const auto entity = static_cast<entt::entity>(possessionService_.subjectEntityId());
    auto& registry = world_.Registry();
    if (!registry.valid(entity))
    {
        possessionService_.release();
        return;
    }

    const auto& transform = registry.get<ce::engine::Transform>(entity);
    viewport_.SetPossessedFeetPosition({ transform.position.x, transform.position.y, transform.position.z });
    juce::Vector3D<float> cameraPosition, cameraTarget;
    if (cameraDirector_.update(world_, physicsWorld_, forward, cameraPosition, cameraTarget))
        viewport_.SetDirectedCameraPose(cameraPosition, cameraTarget);
}

GameClientWindow::GameClientWindow(int clientNumber, juce::ValueTree sceneState,
                                   juce::String gameName, juce::String sceneName,
                                   juce::Array<ce::project::PlayerSlotInfo> playerSlots,
                                   ce::project::GameDocumentInfo game,
                                   const creation::assets::ProjectSession& projectSession,
                                   const creation::suite::SuiteSettings& suiteSettings)
    : DocumentWindow(gameName + " - " + sceneName + " - Client " + juce::String(clientNumber),
                     juce::Colours::black,
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
    setContentOwned(new GameClientContent(clientNumber, sceneState, gameName, sceneName, std::move(playerSlots), std::move(game), projectSession, suiteSettings), true);
    centreWithSize(1280, 720);
    setVisible(true);
}

void GameClientWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace ce::runtime
