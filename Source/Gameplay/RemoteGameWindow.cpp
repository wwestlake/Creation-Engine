#include "RemoteGameWindow.h"

#include "Input/InputActionSystem.h"
#include "Interaction/EditorInteraction.h"
#include "Render/ViewportComponent.h"
#include "Scene/EngineSceneSerializer.h"
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/ui/CreationSuiteLogos.h>
#include <algorithm>

namespace ce::gameplay
{
namespace
{
juce::String Option(const juce::ArgumentList& arguments, const char* name)
{
    const auto index = arguments.indexOfOption(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1].text : juce::String{};
}

class Content final : public juce::Component, private juce::Timer {
public:
    Content(ClientSettings client, creation::assets::ProjectSession project, creation::suite::SuiteSettings suite,
            const project::GameDocumentInfo& game)
        : client_(std::move(client)), project_(std::move(project)), suite_(std::move(suite))
    {
        if (project_.isValid()) input_.LoadForGame(project_, game, error_, "gameplay");
        addAndMakeVisible(viewport_);
        addAndMakeVisible(status_);
        status_.setColour(juce::Label::textColourId, juce::Colours::white);
        status_.setColour(juce::Label::backgroundColourId, juce::Colour(0xc010141a));
        viewport_.SetPlaying(true);
        viewport_.EnterPossessedMode();
        if (!client_.Connect(error_)) status_.setText(error_, juce::dontSendNotification);
        startTimerHz(30);
    }

    void resized() override
    {
        viewport_.setBounds(getLocalBounds());
        status_.setBounds(getLocalBounds().withHeight(30).reduced(8, 0));
    }

private:
    void timerCallback() override
    {
        client_.Pump();
        using State = GameplayClient::State;
        if (client_.ConnectionState() != State::active) {
            const auto reason = client_.Reason();
            status_.setText(error_.isNotEmpty() ? error_ : reason.isNotEmpty() ? reason : "Connecting to the world...",
                            juce::dontSendNotification);
            return;
        }

        if (client_.Baseline() != installed_) {
            const auto& document = client_.Document();
            scene::EngineSceneSerializer::restoreScene(world_, document);
            if (project_.isValid()) viewport_.ResolveProjectAssets(project_, suite_);
            interactions_.select(entt::null);
            installed_ = client_.Baseline();
        }

        const auto poses = client_.InterpolatedPoses();
        {
            std::lock_guard lock(world_.RegistryMutex());
            const auto view = world_.Registry().view<const scene::InstanceId, engine::Transform>();
            for (const auto entity : view) {
                const auto& id = view.get<const scene::InstanceId>(entity).value;
                const auto pose = poses.find(id);
                if (pose == poses.end()) continue;
                auto& transform = view.get<engine::Transform>(entity);
                transform.position = pose->second.position;
                transform.eulerRotationRadians = pose->second.rotation;
            }
        }
        const auto own = poses.find(client_.Entity());
        if (own != poses.end()) {
            const auto feet = scene::ToJuceVector3D(own->second.position);
            const auto forward = viewport_.CameraForward();
            const auto target = feet + juce::Vector3D<float>(0.0f, 1.3f, 0.0f);
            viewport_.SetPossessedFeetPosition(feet);
            viewport_.SetDirectedCameraPose(target - forward * 5.0f + juce::Vector3D<float>(0.0f, 1.0f, 0.0f), target);
        }
        const bool focused = juce::TopLevelWindow::getActiveTopLevelWindow() == getTopLevelComponent();
        input_.PollOncePerFrame(focused);
        const auto level = [&](const char* action, int key) {
            return input_.Bindings().actions.isEmpty()
                ? (focused && juce::Process::isForegroundProcess() && juce::KeyPress::isKeyCurrentlyDown(key) ? 1000 : 0)
                : input_.GetActionValuePerMille(action);
        };
        const int x = level("MoveRight", 'D') - level("MoveLeft", 'A');
        const int z = level("MoveBackward", 'S') - level("MoveForward", 'W');
        client_.Submit({x, z, input_.IsActionActive("Jump")});
        if (input_.WasActionPressed("Interact")) {
            juce::String target;
            {
                std::lock_guard lock(world_.RegistryMutex());
                const auto selected = interactions_.selected();
                const auto& registry = world_.Registry();
                if (registry.valid(selected))
                    if (const auto* id = registry.try_get<scene::InstanceId>(selected)) target = id->value;
            }
            if (target.isNotEmpty()) client_.Request("interact", target);
        }
        status_.setText(client_.Scene() + "   |   Tick " + juce::String(static_cast<juce::int64>(client_.Tick())),
                        juce::dontSendNotification);
        viewport_.PublishFrameSnapshot();
        viewport_.repaint();
    }

    GameplayClient client_;
    creation::assets::ProjectSession project_;
    creation::suite::SuiteSettings suite_;
    juce::String error_;
    engine::World world_;
    interaction::EditorInteraction interactions_{world_};
    input::InputActionSystem input_;
    ViewportComponent viewport_{world_, interactions_, viewport_};
    juce::Label status_;
    std::int64_t installed_ = 0;
};

class Window final : public juce::DocumentWindow {
public:
    explicit Window(std::unique_ptr<Content> content)
        : DocumentWindow("Djehuti Game Client", juce::Colours::black, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
        setContentOwned(content.release(), true);
        centreWithSize(1280, 720);
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};
} // namespace

std::unique_ptr<juce::DocumentWindow> OpenRemoteGame(const juce::ArgumentList& arguments, juce::String& error)
{
    ClientSettings client{Option(arguments, "--join"), Option(arguments, "--gameplay-port").getIntValue(),
        Option(arguments, "--world"), Option(arguments, "--player"),
        juce::SystemStats::getEnvironmentVariable("DJEHUTI_GAMEPLAY_SECRET", {})};
    if (client.address.isEmpty() || !Identifier(client.world) || !Identifier(client.player)
        || client.port < 1 || client.port > 65535 || client.secret.getNumBytesAsUTF8() < 24) {
        error = "Specify --join HOST --gameplay-port PORT --world WORLD --player PLAYER and a gameplay credential.";
        return {};
    }
    creation::assets::ProjectSession projectSession;
    creation::suite::SuiteSettings suite;
    project::GameDocumentInfo game;
    if (const auto id = Option(arguments, "--project"); id.isNotEmpty()) {
        creation::suite::SuiteSettingsStore store;
        suite = store.load(error);
        if (!creation::assets::ProjectWorkspaceService::openProject(suite, id, projectSession, error)) return {};
        juce::Array<project::GameDocumentInfo> games;
        if (!project::EngineGameDocumentStore::loadGames(projectSession, games, error)) return {};
        const auto selected = Option(arguments, "--game");
        const auto found = std::find_if(games.begin(), games.end(), [&](const auto& entry) {
            return selected.isEmpty() || entry.id == selected || entry.name.equalsIgnoreCase(selected);
        });
        if (found == games.end()) { error = "The authored game was not found."; return {}; }
        game = *found;
    }
    return std::make_unique<Window>(
        std::make_unique<Content>(std::move(client), std::move(projectSession), std::move(suite), game));
}
} // namespace ce::gameplay
