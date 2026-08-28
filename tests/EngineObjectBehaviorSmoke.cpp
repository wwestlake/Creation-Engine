#include "Frust/EngineFrustHost.h"
#include "Scene/Components.h"

#include "engine/core_components.h"
#include "engine/world.h"

#include <iostream>
#include <string>

int main()
{
    ce::engine::World world;
    const auto first = world.CreateEntity();
    const auto second = world.CreateEntity();
    world.Registry().emplace<ce::engine::Transform>(first, ce::engine::Transform{});
    world.Registry().emplace<ce::engine::Transform>(second, ce::engine::Transform{});
    world.Registry().emplace<ce::scene::BehaviorAttachments>(first, ce::scene::BehaviorAttachments{ { "set_current_x" } });
    world.Registry().emplace<ce::scene::BehaviorAttachments>(second, ce::scene::BehaviorAttachments{ { "set_current_x" } });

    ce::frust::EngineFrustHost host(world);
    std::string error;
    if (!host.loadObjectBehavior("set_current_x", CE_ENGINE_OBJECT_BEHAVIOR_PLUGIN, error))
    {
        std::cerr << "Could not load object behavior: " << error << '\n';
        return 1;
    }

    host.dispatch(ce::frust::EngineFrustEvent::simulationTick, 19);
    const auto& registry = world.Registry();
    if (registry.get<ce::engine::Transform>(first).position.x != 19.0f ||
        registry.get<ce::engine::Transform>(second).position.x != 19.0f)
    {
        std::cerr << "Object behavior did not receive its current entity context." << '\n';
        return 1;
    }

    std::cout << "Creation Engine object behavior scheduler passed." << '\n';
    return 0;
}
