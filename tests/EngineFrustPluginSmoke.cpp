#include "Frust/EngineFrustHost.h"

#include "engine/core_components.h"
#include "engine/world.h"

#include <cstdint>
#include <iostream>
#include <string>

int main()
{
    ce::engine::World world;
    const auto entity = world.CreateEntity();
    world.Registry().emplace<ce::engine::Transform>(entity, ce::engine::Transform{});
    for (int index = 0; index < 41; ++index) {
        world.AdvanceTick();
    }

    ce::frust::EngineFrustHost host(world);

    std::string error;
    if (!host.load(CE_ENGINE_LIFECYCLE_PLUGIN, error)) {
        std::cerr << "Could not load Engine FRust plugin: " << error << '\n';
        return 1;
    }

    host.dispatch(ce::frust::EngineFrustEvent::simulationTick, 5);
    const auto result = world.Registry().get<ce::engine::Transform>(entity).position.x;
    if (result != 5.0f) {
        std::cerr << "FRust plugin did not update the Engine transform: " << result << '\n';
        return 1;
    }

    std::cout << "Creation Engine FRust transform capability passed." << '\n';
    return 0;
}
