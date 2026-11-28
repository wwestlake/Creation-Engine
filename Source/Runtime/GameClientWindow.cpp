#include "GameClientWindow.h"

#include <creation/ui/CreationSuiteLogos.h>

#include "Scene/EngineSceneSerializer.h"
#include "Scene/Components.h"

#include <cmath>
#include <mutex>

namespace ce::runtime
{

GameClientContent::GameClientContent(int clientNumber, juce::ValueTree sceneState,
                                     project::GameDocumentInfo game, project::SceneDocumentInfo scene,
                                     creation::assets::ProjectSession projectSession,
                                     creation::suite::SuiteSettings suiteSettings, bool autoStart)
    : clientNumber_(clientNumber), projectSession_(std::move(projectSession)), suiteSettings_(std::move(suiteSettings)),
      game_(std::move(game)), scene_(std::move(scene)), initialState_(std::move(sceneState)),
      viewport_(world_, interactions_, viewport_)
{
    physicsWorld_.AttachToWorld(world_);
    frustHost_.setInputActionSystem(&inputActionSystem_);
    if (projectSession_.isValid())
        inputActionSystem_.LoadForGame(projectSession_, game_, inputMappingError_, "gameplay");

    addAndMakeVisible(viewport_);
    hud_.setColour(juce::Label::textColourId, juce::Colours::white);
    hud_.setColour(juce::Label::backgroundColourId, juce::Colour(0xaa10141a));
    hud_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hud_);

    if (autoStart) {
        try {
            (void)runtimeCommands_.Execute(developer::RuntimeAction::start);
        } catch (const developer::Failure&) {
            ShowSceneStatus();
        }
    } else
        ShowSceneStatus();
    startTimerHz(30);
}

GameClientContent::~GameClientContent()
{
    Close();
}

void GameClientContent::Close()
{
    stopTimer();
    Stop();
}

developer::RuntimeOperations GameClientContent::RuntimeOperations()
{
    using namespace developer;
    return {
        [this] { return phase_; },
        [this] { return world_.CurrentTick(); },
        [this](Source source) { return Start(source); },
        [this] { phase_ = Phase::paused; },
        [this] { phase_ = Phase::running; return true; },
        [this] { return Stop(); },
        [this] { Tick(); },
        [this] { return RuntimeDiagnostics(runtimeWorldRunner_, world_, fixture_); }
    };
}

bool GameClientContent::Start(developer::Source requested)
{
    fixture_ = requested == developer::Source::fixture;
    auto scene = scene_;
    auto source = fixture_ ? DeveloperFixture() : FromProject(projectSession_, game_);
    if (fixture_ && !source.game.scenes.isEmpty())
        scene = source.game.scenes.getFirst();
    if (!runtimeWorldRunner_.Start(std::move(source), std::move(scene), fixture_ ? juce::ValueTree{} : initialState_,
                                   [this](juce::String&) { PrepareScene(); return true; })) {
        ShowSceneStatus();
        return false;
    }
    phase_ = developer::Phase::running;
    viewport_.SetPlaying(true);
    ShowSceneStatus();
    return true;
}

bool GameClientContent::Stop()
{
    possessionService_.release();
    cameraDirector_.detach();
    viewport_.ClearDirectedCameraPose();
    viewport_.ExitPossessedMode();
    viewport_.SetPlaying(false);
    runtimeWorldRunner_.Stop();
    phase_ = developer::Phase::stopped;
    return true;
}

void GameClientContent::PrepareScene()
{
    if (projectSession_.isValid())
        viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
    if (!fixture_)
        possessAuthoredPlayer();
}

void GameClientContent::ShowSceneStatus()
{
    const auto& runtimeError = runtimeWorldRunner_.LastError();
    if (runtimeError.isNotEmpty()) {
        hud_.setText(runtimeError, juce::dontSendNotification);
        return;
    }
    hud_.setText(inputMappingError_.isEmpty()
                     ? "CLIENT " + juce::String(clientNumber_) + "  |  " + runtimeWorldRunner_.Game().name
                         + " / " + runtimeWorldRunner_.Scene().name
                     : "INPUT MAPPING ERROR: " + inputMappingError_,
                 juce::dontSendNotification);
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
    if (phase_ == developer::Phase::running)
        Tick();
    viewport_.PublishFrameSnapshot();
    viewport_.repaint();
}

void GameClientContent::Tick()
{
    if (!fixture_)
        inputActionSystem_.PollOncePerFrame(juce::TopLevelWindow::getActiveTopLevelWindow() == getTopLevelComponent());
    runtimeWorldRunner_.Tick({
        [this](float dt) { updatePossessedPlayer(dt); },
        [this] {
            possessionService_.release();
            cameraDirector_.detach();
            viewport_.ClearDirectedCameraPose();
            viewport_.ExitPossessedMode();
        },
        [this] { PrepareScene(); ShowSceneStatus(); }
    });
}

void GameClientContent::possessAuthoredPlayer()
{
    const auto& slots = game_.playerSlots;
    const auto slotId = slots.isEmpty() ? juce::String("player-1") : slots.getFirst().id;
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

    cameraDirector_.setMode(CameraMode::follow);
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
                                   project::GameDocumentInfo game, project::SceneDocumentInfo scene,
                                   creation::assets::ProjectSession projectSession,
                                   creation::suite::SuiteSettings suiteSettings, bool autoStart)
    : DocumentWindow(game.name + " - " + scene.name + " - Client " + juce::String(clientNumber),
                     juce::Colours::black, DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
    setContentOwned(new GameClientContent(clientNumber, std::move(sceneState), std::move(game), std::move(scene),
                                          std::move(projectSession), std::move(suiteSettings), autoStart), true);
    centreWithSize(1280, 720);
    setVisible(true);
}

developer::RuntimeCommands& GameClientWindow::Commands()
{
    return static_cast<GameClientContent&>(*getContentComponent()).Commands();
}

void GameClientWindow::closeButtonPressed()
{
    static_cast<GameClientContent&>(*getContentComponent()).Close();
    setVisible(false);
}

} // namespace ce::runtime
