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
    ServerConfig config = ParseArgs(args);
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
    const auto start = [&](Source requested) {
        ce::runtime::RuntimeGameSource source;
        ce::project::SceneDocumentInfo scene;
        loadError.clear();
        if (requested == Source::fixture) {
            source = ce::runtime::DeveloperFixture();
            if (!source.game.scenes.isEmpty())
                scene = source.game.scenes.getFirst();
        } else if (!OpenAuthoredGame(config, source, scene, loadError)) {
            return false;
        }
        if (worldRuntime.Start(std::move(source), std::move(scene)))
            return true;
        loadError = worldRuntime.LastError();
        return false;
    };
    const auto tickIntervalMs = std::max(1, static_cast<int>(1000.0 / config.tickRateHz));
    const auto tick = [&] { worldRuntime.Tick(); };

    if (!developerSettings.enabled || config.ticks >= 0) {
        if (!start(Source::authored)) {
            std::cerr << "[server] failed to start the runtime: " << loadError << std::endl;
            return 2;
        }
    }

    if (config.ticks >= 0) {
        for (int i = 0; i < config.ticks; ++i) tick();
        std::cout << "[server] completed " << world.CurrentTick() << " ticks" << std::endl;
        return 0;
    }

    if (developerSettings.enabled) {
        developerSettings.allowAuthored = config.projectId.isNotEmpty();
        const bool fixtureAllowed = developerSettings.allowFixture;
        auto phase = Phase::stopped;
        RuntimeCommands runtime({
            [&] { return phase; },
            [&] { return world.CurrentTick(); },
            [&](Source source) {
                if (source == Source::authored && config.projectId.isEmpty()) return false;
                if (!start(source)) return false;
                phase = Phase::running;
                return true;
            },
            [&] { phase = Phase::paused; },
            [&] { phase = Phase::running; return true; },
            [&] { worldRuntime.Stop(); phase = Phase::stopped; return true; },
            tick,
            [&] {
                auto diagnostics = ce::runtime::RuntimeDiagnostics(worldRuntime, world, fixtureAllowed);
                auto* properties = diagnostics.getDynamicObject();
                properties->setProperty("tickRateHz", config.tickRateHz);
                properties->setProperty("authoredConfigured", config.projectId.isNotEmpty());
                if (loadError.isNotEmpty()) properties->setProperty("runtimeError", loadError);
                return diagnostics;
            }
        });
        try {
            auto mount = MakeTestMount(developerSettings);
            DeveloperApi api(std::move(developerSettings), "server", JUCE_APPLICATION_VERSION_STRING, runtime, std::move(mount));
            const auto port = api.Start();
            std::cout << "Developer API http://127.0.0.1:" << port << std::endl;
            while (true) {
                api.PumpOwner();
                if (phase == Phase::running) tick();
                juce::Thread::sleep(tickIntervalMs);
            }
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
    while (true) {
        tick();
        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptimeSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            std::cout << "[server] tick=" << world.CurrentTick() << " uptime=" << uptimeSeconds << "s" << std::endl;
        }
        juce::Thread::sleep(tickIntervalMs);
    }
}
