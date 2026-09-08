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

    // on_event no longer touches the Engine transform (it used to, as a
    // one-time proof that the host-function plumbing worked -- see
    // BuiltInPlugins/EngineLifecycle.frust's own comment. That side effect
    // turned out to be a real, shipped bug: this plugin auto-loads under
    // the engine's own default plugin slot in every real session, so it
    // was silently forcing whatever entity happened to be "first" to a
    // sawtooth position every tick, in every scene, forever). Verify the
    // plumbing still works via on_event's own arithmetic return value
    // instead, and assert the transform is genuinely left alone.
    const std::int64_t tickBeforeDispatch = static_cast<std::int64_t>(world.CurrentTick());
    constexpr std::int64_t simulationTickId = static_cast<std::int64_t>(ce::frust::EngineFrustEvent::simulationTick);
    constexpr std::int64_t argument = 5;
    const auto dispatchResult = host.dispatch(ce::frust::EngineFrustEvent::simulationTick, argument);
    const auto expected = tickBeforeDispatch + simulationTickId + argument;
    if (dispatchResult != expected) {
        std::cerr << "FRust plugin's on_event did not return the expected value: got " << dispatchResult
                   << ", expected " << expected << '\n';
        return 1;
    }

    const auto positionX = world.Registry().get<ce::engine::Transform>(entity).position.x;
    if (positionX != 0.0f) {
        std::cerr << "FRust plugin unexpectedly moved the Engine transform: " << positionX << '\n';
        return 1;
    }

    std::cout << "Creation Engine FRust transform capability passed." << '\n';
    return 0;
}
