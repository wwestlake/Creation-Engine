#include "engine/simulation.h"

#include <vector>

#include "engine/script_component.h"
#include "engine/script_context.h"
#include "engine/script_runtime.h"
#include "engine/world.h"

namespace ce::engine {

void Simulation::Step(World& world, float dt) {
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    auto& registry = world.Registry();

    if (IScriptRuntime* runtime = world.ScriptRuntime(); runtime != nullptr) {
        // Snapshot first: a script's own intrinsics (spawn/destroy) can
        // mutate the registry mid-loop, and iterating a live view while
        // it's being mutated at the same time is undefined behavior per
        // entt -- exactly the pattern World::RegistryMutex()'s own class
        // comment warns about.
        std::vector<entt::entity> scripted;
        for (auto entity : registry.view<ScriptComponent>()) {
            scripted.push_back(entity);
        }

        ScriptContext ctx;
        ctx.world = &world;
        ctx.elapsedTime = world.AdvanceScriptTime(dt);

        for (entt::entity entity : scripted) {
            if (!registry.valid(entity)) {
                continue; // destroyed by an earlier script's intrinsic this same step.
            }
            auto& script = registry.get<ScriptComponent>(entity);
            if (script.compiled == nullptr || script.faulted) {
                continue;
            }

            ctx.faulted = false;
            ctx.faultMessage.clear();

            if (!script.started) {
                script.started = true;
                if (!runtime->CallStart(*script.compiled, entity, ctx)) {
                    script.faulted = true;
                    script.faultMessage = ctx.faultMessage;
                    continue;
                }
            }
            if (!runtime->CallTick(*script.compiled, entity, ctx, dt)) {
                script.faulted = true;
                script.faultMessage = ctx.faultMessage;
            }
        }
    }

    world.AdvanceTick();
}

} // namespace ce::engine
