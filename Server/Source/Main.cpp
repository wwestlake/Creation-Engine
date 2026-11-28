#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "engine/world.h"
#include "Project/EngineGameDocument.h"
#include "Frust/EngineFrustHost.h"
#include "Physics/PhysicsWorld.h"
#include "Runtime/RuntimeDeveloperInspection.h"
#include "DeveloperApi/DeveloperApi.h"
#include "Gameplay/WorldSession.h"
#include "GameplayProcess.h"

#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/suite/SuiteSettings.h>

namespace {

struct ServerConfig {
    double tickRateHz = 30.0;
    // -1 runs the server loop forever. A non-negative value is a useful
    // deterministic smoke-test mode for the host simulation clock.
    int ticks = -1;
    float dt = -1.0f; // <0: derive from tickRateHz.
    juce::String projectId;
    juce::String gameId;
    juce::String sceneId;
};

// Minimal launch-parameter parsing, standing in for the fuller config-file
// story required by capabilities spec section 2.5 ("configure via config
// file or launch parameters, without rebuilding the binary").
ServerConfig ParseArgs(const juce::ArgumentList& args) {
    ServerConfig config;
    if (auto index = args.indexOfOption("--tick-rate"); index >= 0 && index + 1 < args.size()) {
        config.tickRateHz = args[index + 1].text.getDoubleValue();
    }
    if (auto index = args.indexOfOption("--ticks"); index >= 0 && index + 1 < args.size()) {
        config.ticks = args[index + 1].text.getIntValue();
    }
    if (auto index = args.indexOfOption("--dt"); index >= 0 && index + 1 < args.size()) {
        config.dt = static_cast<float>(args[index + 1].text.getDoubleValue());
    }
    if (auto index = args.indexOfOption("--project"); index >= 0 && index + 1 < args.size()) config.projectId = args[index + 1].text;
    if (auto index = args.indexOfOption("--game"); index >= 0 && index + 1 < args.size()) config.gameId = args[index + 1].text;
    if (auto index = args.indexOfOption("--scene"); index >= 0 && index + 1 < args.size()) config.sceneId = args[index + 1].text;
    if (!std::isfinite(config.tickRateHz) || config.tickRateHz < 1.0 || config.tickRateHz > 1000.0)
        config.tickRateHz = 30.0;
    return config;
}

template <typename Document>
const Document* FindDocument(const juce::Array<Document>& documents, const juce::String& idOrName)
{
    for (const auto& document : documents)
        if (document.id == idOrName || document.name.equalsIgnoreCase(idOrName)) return &document;
    return nullptr;
}

bool OpenAuthoredGame(const ServerConfig& config, ce::runtime::RuntimeGameSource& source,
                      ce::project::SceneDocumentInfo& entry, juce::String& error)
{
    if (config.projectId.isEmpty()) return true;
    creation::suite::SuiteSettingsStore settingsStore;
    const auto settings = settingsStore.load(error);
    creation::assets::ProjectSession session;
    if (!creation::assets::ProjectWorkspaceService::openProject(settings, config.projectId, session, error)) return false;
    juce::Array<ce::project::GameDocumentInfo> games;
    if (!ce::project::EngineGameDocumentStore::loadGames(session, games, error)) return false;
    const auto* game = config.gameId.isEmpty() ? (games.isEmpty() ? nullptr : &games.getReference(0)) : FindDocument(games, config.gameId);
    if (game == nullptr) { error = "The requested Engine game was not found."; return false; }
    const auto* scene = config.sceneId.isEmpty()
        ? FindDocument(game->scenes, game->entrySceneId) : FindDocument(game->scenes, config.sceneId);
    if (scene == nullptr) { error = "The requested Engine scene was not found."; return false; }
    entry = *scene;
    source = ce::runtime::FromProject(std::move(session), *game);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit; // pulls in message loop/timer plumbing engine_core relies on.

    const juce::ArgumentList args(argc, argv);
    if (args.containsOption("--join")) return RunGameplayClientProbe(args);
    InstallGameplaySignalHandlers();
    ServerConfig config = ParseArgs(args);
    const auto worldId = GameplayOption(args, "--world");
    const bool local = args.containsOption("--local");
    const bool remote = args.containsOption("--gameplay-port");
    const auto fixturePath = GameplayOption(args, "--gameplay-fixture");
    const auto shutdownPath = GameplayOption(args, "--stop-file");
    const auto shutdownFile = shutdownPath.isEmpty() ? juce::File{}
        : juce::File::getCurrentWorkingDirectory().getChildFile(shutdownPath);
    const auto stopping = [&] { return GameplayStopping() || (shutdownFile != juce::File{} && shutdownFile.existsAsFile()); };
    if ((local && (remote || config.ticks < 0)) || ((local || remote || fixturePath.isNotEmpty()) && worldId.isEmpty())
        || (worldId.isNotEmpty() && config.projectId.isEmpty() && fixturePath.isEmpty())) {
        std::cerr << "[server] Gameplay requires --world and an authored --project. Local console runs also require --ticks." << std::endl;
        return 2;
    }
    using namespace ce::developer;
    Settings developerSettings;
    try {
        developerSettings = ReadSettings();
    } catch (const Failure& failure) {
        std::cerr << "[server] " << failure.message << std::endl;
        return 2;
    }

    ce::engine::World world;
    ce::physics::PhysicsWorld physics;
    physics.AttachToWorld(world);
    ce::frust::EngineFrustHost frustHost(world, true);
    const float stepDt = config.dt >= 0.0f ? config.dt : static_cast<float>(1.0 / config.tickRateHz);
    ce::runtime::RuntimeWorldRunner worldRuntime(world, physics, frustHost, stepDt);

    juce::String loadError;
    std::unique_ptr<ce::gameplay::GameplayStore> store;
    std::unique_ptr<ce::gameplay::WorldSession> authority;
    if (worldId.isNotEmpty()) {
        const auto provider = GameplayOption(args, "--provider");
        const auto interval = GameplayOption(args, "--checkpoint-ticks");
        ce::gameplay::StoreSettings storage{worldId, provider.isEmpty() ? juce::String("memory") : provider,
            GameplayOption(args, "--store"), interval.isEmpty() ? 30 : interval.getIntValue()};
        store = std::make_unique<ce::gameplay::GameplayStore>(std::move(storage));
        if (!store->Open(loadError)) { std::cerr << "[server] " << loadError << std::endl; return 2; }
        ce::gameplay::SessionPolicy policy;
        const auto capacity = GameplayOption(args, "--max-players");
        if (capacity.isNotEmpty()) {
            policy.capacity = capacity.getIntValue();
            if (policy.capacity < 1 || policy.capacity > ce::gameplay::maximumConnections) {
                std::cerr << "[server] Invalid gameplay player capacity." << std::endl;
                return 2;
            }
        }
        if (remote) {
            const auto configFile = juce::File::getCurrentWorkingDirectory().getChildFile(GameplayOption(args, "--credentials"));
            policy.authenticate = ce::gameplay::DevelopmentIdentities(configFile, loadError);
            if (!policy.authenticate) { std::cerr << "[server] " << loadError << std::endl; return 2; }
        }
        authority = std::make_unique<ce::gameplay::WorldSession>(world, physics, frustHost, worldRuntime, *store, std::move(policy));
        if (remote) {
            const auto listen = GameplayOption(args, "--bind");
            const auto value = GameplayOption(args, "--gameplay-port");
            const bool valid = value.isNotEmpty() && value.containsOnly("0123456789") && value.getLargeIntValue() <= 65535;
            if (!valid || !authority->Listen(listen.isEmpty() ? juce::String("127.0.0.1") : listen, value.getIntValue(), loadError)) {
                std::cerr << "[server] Could not start the gameplay listener. " << loadError << std::endl;
                return 2;
            }
        }
    }
    const auto start = [&](Source requested) {
        ce::runtime::RuntimeGameSource source;
        ce::project::SceneDocumentInfo scene;
        loadError.clear();
        if (requested == Source::authored && fixturePath.isNotEmpty()) {
            source = AuthoredGameplayFixture(juce::File::getCurrentWorkingDirectory().getChildFile(fixturePath));
            scene = source.game.scenes.getFirst();
        } else if (requested == Source::fixture) {
            source = ce::runtime::DeveloperFixture();
            if (!source.game.scenes.isEmpty())
                scene = source.game.scenes.getFirst();
        } else if (!OpenAuthoredGame(config, source, scene, loadError)) {
            return false;
        }
        const bool started = authority ? authority->Start(std::move(source), std::move(scene))
                                       : worldRuntime.Start(std::move(source), std::move(scene));
        if (started) return true;
        loadError = authority ? authority->Error() : worldRuntime.LastError();
        return false;
    };
    const auto tickIntervalMs = std::max(1, static_cast<int>(1000.0 / config.tickRateHz));
    const auto tick = [&] { if (authority) authority->Tick(); else worldRuntime.Tick(); };
    const auto stop = [&] { if (authority) return authority->Stop(); worldRuntime.Stop(); return true; };

    if (authority || !developerSettings.enabled || config.ticks >= 0) {
        if (!start(Source::authored)) {
            std::cerr << "[server] failed to start the runtime: " << loadError << std::endl;
            return 2;
        }
    }

    if (local) return RunLocalGameplayProbe(*authority, args, config.ticks);
    if (remote) std::cout << "Gameplay UDP port " << authority->Port() << std::endl;
    if (config.ticks >= 0) {
        for (int i = 0; i < config.ticks; ++i) { if (authority) authority->Pump(); tick(); }
        std::cout << "[server] completed " << world.CurrentTick() << " ticks" << std::endl;
        return stop() ? 0 : 2;
    }

    if (developerSettings.enabled) {
        developerSettings.allowAuthored = config.projectId.isNotEmpty() || fixturePath.isNotEmpty();
        if (authority) developerSettings.allowFixture = false;
        const bool fixtureAllowed = developerSettings.allowFixture;
        auto phase = worldRuntime.IsPlaying() ? Phase::running : Phase::stopped;
        RuntimeCommands runtime({
            [&] { return phase; },
            [&] { return world.CurrentTick(); },
            [&](Source source) {
                if (source == Source::authored && config.projectId.isEmpty() && fixturePath.isEmpty()) return false;
                if (!start(source)) return false;
                phase = Phase::running;
                return true;
            },
            [&] { phase = Phase::paused; },
            [&] { phase = Phase::running; return true; },
            [&] { if (!stop()) return false; phase = Phase::stopped; return true; },
            tick,
            [&] {
                auto diagnostics = ce::runtime::RuntimeDiagnostics(worldRuntime, world, fixtureAllowed);
                auto* properties = diagnostics.getDynamicObject();
                properties->setProperty("tickRateHz", config.tickRateHz);
                properties->setProperty("authoredConfigured", config.projectId.isNotEmpty() || fixturePath.isNotEmpty());
                if (authority) properties->setProperty("gameplay", authority->Diagnostics());
                if (loadError.isNotEmpty()) properties->setProperty("runtimeError", loadError);
                return diagnostics;
            }
        });
        try {
            auto mount = store ? store->InspectableMount() : MakeTestMount(developerSettings);
            DeveloperApi api(std::move(developerSettings), "server", JUCE_APPLICATION_VERSION_STRING, runtime, std::move(mount));
            const auto port = api.Start();
            std::cout << "Developer API http://127.0.0.1:" << port << std::endl;
            while (!stopping()) {
                api.PumpOwner();
                if (authority) authority->Pump();
                if (phase == Phase::running) tick();
                juce::Thread::sleep(tickIntervalMs);
            }
            return stop() ? 0 : 2;
        } catch (const Failure& failure) {
            std::cerr << "[server] " << failure.message << std::endl;
            return 2;
        } catch (...) {
            std::cerr << "[server] Developer API host failed." << std::endl;
            return 2;
        }
    }

    std::cout << "Creation Engine dedicated server starting at " << config.tickRateHz << " Hz." << std::endl;
    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    while (!stopping()) {
        if (authority) authority->Pump();
        tick();
        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptimeSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            std::cout << "[server] tick=" << world.CurrentTick() << " uptime=" << uptimeSeconds << "s" << std::endl;
        }
        juce::Thread::sleep(tickIntervalMs);
    }
    return stop() ? 0 : 2;
}
