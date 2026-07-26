#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "engine/world.h"

namespace {

struct ServerConfig {
    int port = 7777;
    double tickRateHz = 30.0;
};

// Minimal launch-parameter parsing, standing in for the fuller config-file
// story required by spec section 2.5 ("configure via config file or
// launch parameters, without rebuilding the binary").
ServerConfig ParseArgs(int argc, char** argv) {
    ServerConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.port = std::atoi(argv[++i]);
        } else if (arg == "--tick-rate" && i + 1 < argc) {
            config.tickRateHz = std::atof(argv[++i]);
        }
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    ServerConfig config = ParseArgs(argc, argv);
    ce::engine::World world;

    const auto tickDuration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / config.tickRateHz));

    std::printf("Creation Engine dedicated server starting on port %d at %.1f Hz.\n", config.port,
                config.tickRateHz);

    const auto startTime = std::chrono::steady_clock::now();
    auto nextTick = startTime;

    // Placeholder authoritative loop: advances the world clock at a fixed
    // rate and reports basic operational visibility (section 2.5). Network
    // accept/session management, input validation, and rule-graph
    // execution (section 2.2) land on top of this scaffold.
    while (true) {
        world.AdvanceTick();

        if (world.CurrentTick() % static_cast<ce::engine::Tick>(config.tickRateHz * 10) == 0) {
            const auto uptime = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
            std::printf("[server] tick=%llu uptime=%.1fs\n",
                        static_cast<unsigned long long>(world.CurrentTick()), uptime);
        }

        nextTick += tickDuration;
        std::this_thread::sleep_until(nextTick);
    }
}
