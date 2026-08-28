#include "engine/simulation.h"

#include "engine/world.h"

namespace ce::engine {

void Simulation::Step(World& world, float dt) {
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    (void) dt;
    world.AdvanceTick();
}

} // namespace ce::engine
