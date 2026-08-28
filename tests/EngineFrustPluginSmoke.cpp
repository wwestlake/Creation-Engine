#include <creation/frust/PluginRuntime.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace
{
std::int64_t currentTick()
{
    return 41;
}
}

int main()
{
    creation::frust::PluginRuntime runtime("creation-engine");
    runtime.registerHostFunction("engine_current_tick", reinterpret_cast<void*>(&currentTick));

    std::string error;
    if (!runtime.load(CE_ENGINE_LIFECYCLE_PLUGIN, error)) {
        std::cerr << "Could not load Engine FRust plugin: " << error << '\n';
        return 1;
    }

    const auto result = runtime.callEvent(3, 5);
    if (result != 49) {
        std::cerr << "Unexpected FRust lifecycle result: " << result << '\n';
        return 1;
    }

    std::cout << "Creation Engine FRust lifecycle plugin passed." << '\n';
    return 0;
}
