#include <JuceHeader.h>

#include <iostream>

#include "engine/simulation.h"
#include "engine/world.h"
#include "Frust/EngineFrustHost.h"

namespace {

struct ServerConfig {
    int port = 7777;
    double tickRateHz = 30.0;
    // -1 runs the server loop forever. A non-negative value is a useful
    // deterministic smoke-test mode for the host simulation clock.
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
    ce::frust::EngineFrustHost frustHost(world, true);
    std::string frustError;
    if (!frustHost.loadBundled(frustError)) {
        std::cerr << "[server] failed to load authoritative FRust plugins: " << frustError << std::endl;
        return 1;
    }

    const float stepDt = config.dt >= 0.0f ? config.dt : static_cast<float>(1.0 / config.tickRateHz);

    if (config.ticks >= 0) {
        frustHost.beginPlay(0);
        for (int i = 0; i < config.ticks; ++i) {
            ce::engine::Simulation::Step(world, stepDt);
            frustHost.tick(static_cast<std::int64_t>(world.CurrentTick()));
        }
        frustHost.endPlay(static_cast<std::int64_t>(world.CurrentTick()));
        std::cout << "[server] completed " << world.CurrentTick() << " ticks" << std::endl;
        return 0;
    }

    const auto tickIntervalMs = static_cast<int>(1000.0 / config.tickRateHz);

    std::cout << "Creation Engine dedicated server starting on port " << config.port << " at " << config.tickRateHz
              << " Hz." << std::endl;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    frustHost.beginPlay(0);

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
        frustHost.tick(static_cast<std::int64_t>(world.CurrentTick()));

        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptimeSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            std::cout << "[server] tick=" << world.CurrentTick() << " uptime=" << uptimeSeconds << "s" << std::endl;
        }

        juce::Thread::sleep(tickIntervalMs);
    }
}
