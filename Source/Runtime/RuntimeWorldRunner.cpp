#include "RuntimeWorldRunner.h"

#include "Frust/EngineFrustHost.h"
#include "Frust/PodCatalog.h"
#include "Input/InputBindingDocumentStore.h"
#include "Physics/PhysicsWorld.h"
#include "engine/foundation_gameplay.h"
#include "engine/simulation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ce::runtime
{

RuntimeGameSource FromProject(creation::assets::ProjectSession session, project::GameDocumentInfo game)
{
    const auto sessionView = std::make_shared<creation::assets::ProjectSession>(std::move(session));
    return {
        game,
        [sessionView, game](const auto& scene, auto& state, auto& error) {
            return project::EngineGameDocumentStore::readSceneState(*sessionView, game, scene, state, error);
        },
        [sessionView](auto& catalog, auto& error) { return catalog.LoadAll(*sessionView, error); },
        [sessionView](const std::string& name) {
            for (const auto& asset : sessionView->getManifest().assetCatalog.query({}))
                if (asset.displayName == juce::String(name)) return true;
            return false;
        },
        [sessionView, codeRoot = game.codeRoot()](const std::string& name, std::string& source) {
            juce::MemoryBlock data;
            if (!sessionView->readEntry(codeRoot + juce::String(name), data))
                return false;
            source = data.toString().toStdString();
            return true;
        },
        sessionView->isValid() ? input::InputBindingDocumentStore::listComboNames(*sessionView, game)
                               : std::vector<std::string>{}
    };
}

RuntimeWorldRunner::RuntimeWorldRunner(engine::World& world, physics::PhysicsWorld& physics,
                                       frust::EngineFrustHost& frust, float stepSeconds)
    : world_(world), physics_(physics), frust_(frust),
      stepSeconds_(std::isfinite(stepSeconds) && stepSeconds >= 0.0f ? stepSeconds : defaultStepSeconds)
{
    frust_.setPhysicsWorld(&physics_);
    frust_.setActiveGameIdProvider([this] { return source_.game.id.toStdString(); });
    frust_.setActiveSceneIdProvider([this] { return scene_.id.toStdString(); });
    frust_.setSceneTransitionRequestHandler([this](const std::string& reference) { pendingScene_ = reference; });
    frust_.setAssetExistsProvider([this](const std::string& name) {
        return source_.assetExists && source_.assetExists(name);
    });
}

RuntimeWorldRunner::~RuntimeWorldRunner()
{
    Stop();
    frust_.setActiveGameIdProvider({});
    frust_.setActiveSceneIdProvider({});
    frust_.setSceneTransitionRequestHandler({});
    frust_.setAssetExistsProvider({});
}

bool RuntimeWorldRunner::Start(RuntimeGameSource source, project::SceneDocumentInfo scene, juce::ValueTree state,
                               const std::function<bool(juce::String&)>& prepareHost)
{
    if (playing_)
        return false;

    error_.clear();
    source_ = std::move(source);
    scene_ = std::move(scene);
    pendingScene_.clear();
    world_.ResetTick();

    std::string nativeError;
    if ((!frust_.isLoaded() && !frust_.loadBundled(nativeError))
        || !frust_.RefreshComboEventNodes(source_.comboNames, nativeError))
        error_ = juce::String(nativeError);

    if (error_.isEmpty() && !state.isValid()) {
        if (source_.readScene) {
            if (!source_.readScene(scene_, state, error_) && error_.isEmpty())
                error_ = "Could not read scene " + scene_.name;
        } else {
            state = juce::ValueTree("CreationEngineScene");
        }
    }

    bool restored = false;
    if (error_.isEmpty()) {
        restored = project::EngineGameDocumentStore::restoreSceneState(world_, scene_, state);
        if (!restored)
            error_ = "Could not restore scene " + scene_.name;
        else
            physics_.ResetTimeline();
    }

    if (error_.isEmpty() && LoadBehaviors() && (!prepareHost || prepareHost(error_))) {
        frust_.beginPlay(0);
        playing_ = true;
        return true;
    }

    if (error_.isEmpty())
        error_ = "Could not start the runtime.";
    frust_.unloadObjectBehaviors();
    if (restored) {
        {
            std::lock_guard lock(world_.RegistryMutex());
            world_.Registry().clear();
        }
        physics_.ResetTimeline();
    }
    source_ = {};
    scene_ = {};
    pendingScene_.clear();
    return false;
}

void RuntimeWorldRunner::Stop()
{
    if (playing_) {
        frust_.releaseLevel(static_cast<std::int64_t>(world_.CurrentTick()));
        frust_.unloadObjectBehaviors();
        {
            std::lock_guard lock(world_.RegistryMutex());
            world_.Registry().clear();
        }
        physics_.ResetTimeline();
        world_.ResetTick();
    }
    playing_ = false;
    pendingScene_.clear();
    scene_ = {};
    source_ = {};
}

bool RuntimeWorldRunner::LoadBehaviors()
{
    const auto ids = frust_.behaviorIds();
    if (ids.empty())
        return true;

    frust::PodCatalog catalog;
    if (!source_.readPods) {
        error_ = "The scene has attached behaviors but no behavior catalog.";
        return false;
    }
    if (!source_.readPods(catalog, error_))
        return false;

    for (const auto& id : ids) {
        if (frust_.isObjectBehaviorLoaded(id))
            continue;
        auto* entry = catalog.FindEntry(juce::String(id));
        if (entry == nullptr || entry->kind != frust::PodKind::Behavior) {
            error_ = "Attached Behavior Pod was not found: " + juce::String(id);
            return false;
        }
        std::string source, error;
        if (!frust::CompilePodSource(id, *entry, entry->graph, frust_.nodeLibraries(), source, error)
            || !frust_.loadObjectBehaviorSource(id, source, source_.readModule, error)) {
            error_ = "Behavior " + juce::String(id) + ": " + juce::String(error);
            return false;
        }
    }
    return true;
}

void RuntimeWorldRunner::ApplyTransition(const Hooks& hooks)
{
    if (pendingScene_.isEmpty())
        return;
    const auto requested = std::exchange(pendingScene_, {});
    const auto& scenes = source_.game.scenes;
    auto target = std::find_if(scenes.begin(), scenes.end(), [&](const auto& scene) { return scene.id == requested; });
    if (target == scenes.end())
        target = std::find_if(scenes.begin(), scenes.end(), [&](const auto& scene) { return scene.name.equalsIgnoreCase(requested); });
    if (target == scenes.end() || !source_.readScene) {
        error_ = "Scene transition target was not found: " + requested;
        return;
    }

    juce::ValueTree state;
    error_.clear();
    if (!source_.readScene(*target, state, error_) || !state.hasType("CreationEngineScene")) {
        if (error_.isEmpty()) error_ = "Invalid scene data: " + target->name;
        return;
    }

    if (hooks.beforeSceneChange)
        hooks.beforeSceneChange();
    const auto tick = static_cast<std::int64_t>(world_.CurrentTick());
    frust_.releaseLevel(tick);
    pendingScene_.clear(); // The outgoing scene can no longer request another transition.
    project::EngineGameDocumentStore::restoreSceneState(world_, *target, state);
    scene_ = *target;
    physics_.ResetTimeline();
    LoadBehaviors();
    frust_.beginPlay(tick);
    if (hooks.afterSceneChange)
        hooks.afterSceneChange();
}

void RuntimeWorldRunner::Tick(const Hooks& hooks)
{
    if (!playing_)
        return;

    engine::Simulation::Step(world_, stepSeconds_);
    engine::FoundationGameplay::Step(world_, {}, stepSeconds_);
    frust_.prePhysicsTick(static_cast<std::int64_t>(world_.CurrentTick()));

    const float physicsAlpha = physics_.Advance(world_, stepSeconds_);
    {
        std::lock_guard lock(world_.RegistryMutex());
        physics_.InterpolateTransforms(world_, physicsAlpha);
    }
    if (hooks.afterPhysics)
        hooks.afterPhysics(stepSeconds_);

    frust_.postPhysicsTick(static_cast<std::int64_t>(world_.CurrentTick()));
    ApplyTransition(hooks);
}

} // namespace ce::runtime
