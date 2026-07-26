#include <JuceHeader.h>

#include <iostream>

#include "engine/world.h"

namespace {

struct ServerConfig {
    int port = 7777;
    double tickRateHz = 30.0;
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
    return config;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit; // pulls in message loop/timer plumbing engine_core relies on.

    const juce::ArgumentList args(argc, argv);
    ServerConfig config = ParseArgs(args);
    ce::engine::World world;

    const auto tickIntervalMs = static_cast<int>(1000.0 / config.tickRateHz);

    std::cout << "Creation Engine dedicated server starting on port " << config.port << " at " << config.tickRateHz
              << " Hz." << std::endl;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();

    // Placeholder authoritative loop: advances the world clock at a fixed
    // rate and reports basic operational visibility (section 2.5). Network
    // accept/session management, input validation, and rule-graph
    // execution (section 2.2) land on top of this scaffold.
    while (true) {
        world.AdvanceTick();

        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptimeSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            std::cout << "[server] tick=" << world.CurrentTick() << " uptime=" << uptimeSeconds << "s" << std::endl;
        }

        juce::Thread::sleep(tickIntervalMs);
    }
}
