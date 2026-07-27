#include <JuceHeader.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "engine/core_components.h"
#include "engine/script_component.h"
#include "engine/simulation.h"
#include "engine/world.h"
#include "lang/jit/script_runtime.h"

namespace {

struct ServerConfig {
    int port = 7777;
    double tickRateHz = 30.0;
    juce::String scriptPath;
    // -1: run forever (the real dedicated-server loop below). >= 0: run
    // exactly this many Simulation::Step calls then dump state and
    // exit -- a deterministic headless batch mode, not a "real" server
    // feature, that exists so `--script`'s behavior can be verified by
    // CTest (see Language/tests/simulation/) and cross-checked
    // bit-for-bit against celc --run-simulation (same script, same
    // ticks, same dt) without needing a running network client.
    int ticks = -1;
    float dt = -1.0f; // <0: derive from tickRateHz.
};

// Minimal launch-parameter parsing, standing in for the fuller config-file
// story required by capabilities spec section 2.5 ("configure via config
// file or launch parameters, without rebuilding the binary").
ServerConfig ParseArgs(const juce::ArgumentList& args) {
    ServerConfig config;
    if (auto index = args.indexOfOption("--port"); index >= 0 && index + 1 < args.size()) {
        config.port = args[index + 1].text.getIntValue();
    }
    if (auto index = args.indexOfOption("--tick-rate"); index >= 0 && index + 1 < args.size()) {
        config.tickRateHz = args[index + 1].text.getDoubleValue();
    }
    if (auto index = args.indexOfOption("--script"); index >= 0 && index + 1 < args.size()) {
        config.scriptPath = args[index + 1].text;
    }
    if (auto index = args.indexOfOption("--ticks"); index >= 0 && index + 1 < args.size()) {
        config.ticks = args[index + 1].text.getIntValue();
    }
    if (auto index = args.indexOfOption("--dt"); index >= 0 && index + 1 < args.size()) {
        config.dt = static_cast<float>(args[index + 1].text.getDoubleValue());
    }
    return config;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit; // pulls in message loop/timer plumbing engine_core relies on.

    const juce::ArgumentList args(argc, argv);
    ServerConfig config = ParseArgs(args);
    ce::engine::World world;

    entt::entity scriptEntity = entt::null;
    if (config.scriptPath.isNotEmpty()) {
        std::ifstream file(config.scriptPath.toStdString());
        if (!file) {
            std::cerr << "[server] cannot open script " << config.scriptPath << std::endl;
            return 1;
        }
        std::ostringstream sourceStream;
        sourceStream << file.rdbuf();

        auto runtime = ce::lang::jit::CreateScriptRuntime();
        std::string error;
        auto compiled = runtime->Compile(sourceStream.str(), error);
        if (compiled == nullptr) {
            std::cerr << "[server] script compile failed: " << error << std::endl;
            return 1;
        }
        world.SetScriptRuntime(runtime);
        scriptEntity = world.CreateEntity();
        world.Registry().emplace<ce::engine::ScriptComponent>(scriptEntity, ce::engine::ScriptComponent{ compiled });
    }

    const float stepDt = config.dt >= 0.0f ? config.dt : static_cast<float>(1.0 / config.tickRateHz);

    if (config.ticks >= 0) {
        // Headless batch mode -- see ServerConfig::ticks's own comment.
        for (int i = 0; i < config.ticks; ++i) {
            ce::engine::Simulation::Step(world, stepDt);
        }

        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        if (scriptEntity != entt::null && world.Registry().valid(scriptEntity)) {
            const auto& script = world.Registry().get<ce::engine::ScriptComponent>(scriptEntity);
            const auto* transform = world.Registry().try_get<ce::engine::Transform>(scriptEntity);
            const ce::engine::Vec3 p = transform != nullptr ? transform->position : ce::engine::Vec3{};
            std::cout << std::setprecision(9) << p.x << " " << p.y << " " << p.z << "\n";
            std::cout << std::floor(p.x * p.x + p.z * p.z + 0.5f) << std::endl;
            if (script.faulted) {
                std::cerr << "[server] script faulted: " << script.faultMessage << std::endl;
                return 1;
            }
        }
        return 0;
    }

    const auto tickIntervalMs = static_cast<int>(1000.0 / config.tickRateHz);

    std::cout << "Creation Engine dedicated server starting on port " << config.port << " at " << config.tickRateHz
              << " Hz." << std::endl;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();

    // The real dedicated-server loop: advances the world clock (and any
    // attached ScriptComponents) at a fixed rate and reports basic
    // operational visibility (section 2.5). Network accept/session
    // management and input validation (section 2.2) land on top of this
    // scaffold. Uses the SAME Simulation::Step call the editor's
    // MainComponent::timerCallback uses when playing -- the concrete
    // implementation of "the same rule module runs identically
    // client-side or server-side."
    while (true) {
        ce::engine::Simulation::Step(world, stepDt);

        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptimeSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            std::cout << "[server] tick=" << world.CurrentTick() << " uptime=" << uptimeSeconds << "s" << std::endl;
        }

        juce::Thread::sleep(tickIntervalMs);
    }
}
