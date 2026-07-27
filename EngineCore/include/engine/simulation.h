#pragma once

namespace ce::engine {

class World;

// The one tick body both the editor (MainComponent::timerCallback, when
// playing) and CreationEngineServer's main loop call -- the concrete
// implementation of the "the same rule module runs identically
// client-side or server-side" requirement. A static function taking
// World& rather than a class instance: there is exactly one Simulation
// per running process and it holds no state of its own beyond what's
// already on World (World::ScriptRuntime(), World::CurrentScriptTime()).
class Simulation {
public:
    // Runs every ScriptComponent's on_tick (calling on_start first if
    // this is that entity's first tick), then advances World's tick
    // counter. A no-op script phase (but tick still advances) if
    // world.ScriptRuntime() is null -- headless tools with no scripting
    // need don't have to inject a stand-in runtime just to call Step.
    // `dt` is expected to be a fixed timestep, never wall-clock, so a
    // recorded script run is reproducible.
    static void Step(World& world, float dt);
};

} // namespace ce::engine
