#include <JuceHeader.h>

#include <iostream>

#include "engine/world.h"
#include "Project/EngineGameDocument.h"
#include "Frust/EngineFrustHost.h"
#include "Physics/PhysicsWorld.h"
#include "Runtime/RuntimeWorldRunner.h"

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
    return config;
}

template <typename Document>
const Document* FindDocument(const juce::Array<Document>& documents, const juce::String& idOrName)
{
    for (const auto& document : documents)
        if (document.id == idOrName || document.name.equalsIgnoreCase(idOrName)) return &document;
    return nullptr;
}

bool LoadAuthoredWorld(const ServerConfig& config, ce::engine::World& world, juce::String& error)
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
    return ce::project::EngineGameDocumentStore::loadScene(session, *game, *scene, world, error);
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit; // pulls in message loop/timer plumbing engine_core relies on.

    const juce::ArgumentList args(argc, argv);
    ServerConfig config = ParseArgs(args);
    ce::engine::World world;
    juce::String loadError;
    if (!LoadAuthoredWorld(config, world, loadError)) {
        std::cerr << "[server] failed to load authored world: " << loadError << std::endl;
        return 2;
    }

    ce::physics::PhysicsWorld physicsWorld;
    physicsWorld.AttachToWorld(world);
    ce::frust::EngineFrustHost frustHost(world, true);
    frustHost.setPhysicsWorld(&physicsWorld);
    ce::runtime::RuntimeWorldRunner runtime(world, physicsWorld, frustHost);
    std::string frustError;
    if (!frustHost.loadBundled(frustError)) {
        std::cerr << "[server] failed to load authoritative FRust plugins: " << frustError << std::endl;
        return 1;
    }

    const float stepDt = config.dt >= 0.0f ? config.dt : static_cast<float>(1.0 / config.tickRateHz);

    if (config.ticks >= 0) {
        runtime.BeginPlay();
        for (int i = 0; i < config.ticks; ++i) {
            runtime.RunFrame(stepDt);
        }
        runtime.EndPlay();
        std::cout << "[server] completed " << world.CurrentTick() << " ticks" << std::endl;
        return 0;
    }

    const auto tickIntervalMs = static_cast<int>(1000.0 / config.tickRateHz);

    std::cout << "[server] headless simulation running at " << config.tickRateHz << " Hz." << std::endl;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    runtime.BeginPlay();

    while (true) {
        runtime.RunFrame(stepDt);

        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptimeSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            std::cout << "[server] tick=" << world.CurrentTick() << " uptime=" << uptimeSeconds << "s" << std::endl;
        }

        juce::Thread::sleep(tickIntervalMs);
    }
}
