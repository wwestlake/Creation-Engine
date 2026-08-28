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
    const auto* selectNode = host.nodeLibraries().FindNodeType("core_select");
    if (selectNode == nullptr || selectNode->frustEntryPoint != "core_select" ||
        selectNode->inputs.size() != 1 || selectNode->outputs.size() != 1)
    {
        std::cerr << "Object behavior plugin did not register its generic node library." << '\n';
        return 1;
    }
    const auto* triggerNode = host.nodeLibraries().FindNodeType("core_trigger");
    const auto* repeatNode = host.nodeLibraries().FindNodeType("core_repeat");
    if (triggerNode == nullptr || triggerNode->outputs.size() != 1 ||
        triggerNode->outputs.front().name != "then" || repeatNode == nullptr ||
        repeatNode->outputs.size() != 2 || repeatNode->outputs.front().name != "body" ||
        repeatNode->outputs.back().name != "completed")
    {
        std::cerr << "FRust callable or loop node reflection did not preserve execution exits." << '\n';
        return 1;
    }

    const auto& registry = world.Registry();
    host.prepareLevel(7);
    if (registry.get<ce::engine::Transform>(first).position.x != 107.0f ||
        registry.get<ce::engine::Transform>(second).position.x != 107.0f)
    {
        std::cerr << "Object behavior did not receive spawn during level preparation." << '\n';
        return 1;
    }

    host.beginPlay(10);
    if (registry.get<ce::engine::Transform>(first).position.x != 210.0f ||
        registry.get<ce::engine::Transform>(second).position.x != 210.0f)
    {
        std::cerr << "Object behavior did not receive begin-play." << '\n';
        return 1;
    }

    host.tick(19);
    if (registry.get<ce::engine::Transform>(first).position.x != 19.0f ||
        registry.get<ce::engine::Transform>(second).position.x != 19.0f)
    {
        std::cerr << "Object behavior did not receive its current entity context." << '\n';
        return 1;
    }

    host.endPlay(20);
    if (registry.get<ce::engine::Transform>(first).position.x != -320.0f ||
        registry.get<ce::engine::Transform>(second).position.x != -320.0f)
    {
        std::cerr << "Object behavior did not receive end-play." << '\n';
        return 1;
    }

    host.notifyObjectDestroyed(first, 21);
    if (registry.get<ce::engine::Transform>(first).position.x != -421.0f ||
        registry.get<ce::engine::Transform>(second).position.x != -320.0f)
    {
        std::cerr << "Object behavior did not receive destroy, or affected another object." << '\n';
        return 1;
    }

    std::cout << "Creation Engine object behavior scheduler passed." << '\n';
    return 0;
}
