#include "GameClientWindow.h"

#include <creation/ui/CreationSuiteLogos.h>
#include <juce_cryptography/juce_cryptography.h>

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
        [this] {
            auto diagnostics = RuntimeDiagnostics(runtimeWorldRunner_, world_, fixture_);
            if (authority_) developer::Put(diagnostics, "gameplay", authority_->Diagnostics());
            return diagnostics;
        }
    };
}

bool GameClientContent::Start(developer::Source requested)
{
    fixture_ = requested == developer::Source::fixture;
    auto scene = scene_;
    auto source = fixture_ ? DeveloperFixture() : FromProject(projectSession_, game_);
    if (fixture_ && !source.game.scenes.isEmpty())
        scene = source.game.scenes.getFirst();
    const auto prepare = [this](juce::String&) { PrepareScene(); return true; };
    if (fixture_) {
        if (!runtimeWorldRunner_.Start(std::move(source), std::move(scene), {}, prepare)) {
            ShowSceneStatus();
            return false;
        }
    } else {
        if (!authority_) {
            const auto environment = [](const char* key) { return juce::SystemStats::getEnvironmentVariable(key, {}); };
            auto worldId = environment("DJEHUTI_LOCAL_WORLD");
            if (worldId.isEmpty()) {
                const auto key = projectSession_.getProjectId() + "/" + game_.id;
                const auto hash = juce::SHA256(key.toRawUTF8(), key.getNumBytesAsUTF8()).toHexString();
                worldId = "local-" + hash.substring(0, 32);
            }
            auto provider = environment("DJEHUTI_LOCAL_PROVIDER");
            if (provider.isEmpty()) provider = "memory";
            gameplayStore_ = std::make_unique<gameplay::GameplayStore>(
                gameplay::StoreSettings{std::move(worldId), std::move(provider), environment("DJEHUTI_LOCAL_STORE"), 30});
            authority_ = std::make_unique<gameplay::WorldSession>(world_, physicsWorld_, frustHost_,
                                                                runtimeWorldRunner_, *gameplayStore_);
        }
        if (!authority_->Start(std::move(source), std::move(scene), initialState_, prepare) || !authority_->AdmitLocal()) {
            authority_->Stop();
            ShowSceneStatus();
            return false;
        }
    }
    phase_ = developer::Phase::running;
    viewport_.SetPlaying(true);
    ShowSceneStatus();
    return true;
}

bool GameClientContent::Stop()
{
    cameraDirector_.detach();
    cameraTarget_ = -1;
    viewport_.ClearDirectedCameraPose();
    viewport_.ExitPossessedMode();
    viewport_.SetPlaying(false);
    if (authority_ && !fixture_) {
        if (!authority_->Stop()) { ShowSceneStatus(); return false; }
    } else {
        runtimeWorldRunner_.Stop();
    }
    phase_ = developer::Phase::stopped;
    return true;
}

void GameClientContent::PrepareScene()
{
    if (projectSession_.isValid())
        viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
}

void GameClientContent::ShowSceneStatus()
{
    const auto runtimeError = authority_ && authority_->Error().isNotEmpty()
        ? authority_->Error() : runtimeWorldRunner_.LastError();
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
    const RuntimeWorldRunner::Hooks hooks{
        [this](float) { updatePlayerCamera(); },
        [this] {
            cameraDirector_.detach();
            cameraTarget_ = -1;
            viewport_.ClearDirectedCameraPose();
            viewport_.ExitPossessedMode();
        },
        [this] { PrepareScene(); ShowSceneStatus(); },
        {}
    };
    if (!authority_ || fixture_) {
        runtimeWorldRunner_.Tick(hooks);
        return;
    }
    const auto level = [this](const char* name) { return inputActionSystem_.GetActionValuePerMille(name); };
    const float sideways = static_cast<float>(level("MoveRight") - level("MoveLeft"));
    const float forwards = static_cast<float>(level("MoveForward") - level("MoveBackward"));
    const auto view = viewport_.CameraForward();
    const float yaw = std::atan2(view.x, -view.z);
    authority_->SubmitLocal({juce::jlimit(-1000, 1000, juce::roundToInt(sideways * std::cos(yaw) + forwards * std::sin(yaw))),
                             juce::jlimit(-1000, 1000, juce::roundToInt(sideways * std::sin(yaw) - forwards * std::cos(yaw))),
                             inputActionSystem_.IsActionActive("Jump")});
    if (inputActionSystem_.WasActionPressed("Interact")) {
        juce::String target;
        {
            std::lock_guard lock(world_.RegistryMutex());
            const auto entity = interactions_.selected();
            const auto& registry = world_.Registry();
            if (registry.valid(entity))
                if (const auto* id = registry.try_get<scene::InstanceId>(entity)) target = id->value;
        }
        if (target.isNotEmpty()) authority_->SubmitLocalRequest("interact", target);
    }
    authority_->Tick(hooks);
}

void GameClientContent::updatePlayerCamera()
{
    if (!authority_) return;
    const auto subject = authority_->LocalHandle();
    if (subject < 0) return;
    if (subject != cameraTarget_) {
        cameraTarget_ = subject;
        cameraDirector_.setMode(CameraMode::follow);
        cameraDirector_.attach(subject);
        viewport_.EnterPossessedMode();
    }
    if (inputActionSystem_.WasActionPressed("CameraMode")) cameraDirector_.cycleMode();

    {
        std::lock_guard lock(world_.RegistryMutex());
        const auto entity = static_cast<entt::entity>(subject);
        const auto& registry = world_.Registry();
        if (const auto* transform = registry.try_get<engine::Transform>(entity))
            viewport_.SetPossessedFeetPosition(scene::ToJuceVector3D(transform->position));
    }
    juce::Vector3D<float> position, target;
    if (cameraDirector_.update(world_, physicsWorld_, viewport_.CameraForward(), position, target))
        viewport_.SetDirectedCameraPose(position, target);
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
