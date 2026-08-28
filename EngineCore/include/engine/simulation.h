#pragma once

namespace ce::engine {

class World;

// The one tick body both the editor (MainComponent::timerCallback, when
// playing) and CreationEngineServer's main loop call -- the concrete
// implementation of the "the same rule module runs identically
// client-side or server-side" requirement. A static function taking
// World& rather than a class instance: there is exactly one Simulation
// per running process and it holds no state beyond the World clock.
class Simulation {
public:
    // Advances the World tick. FRust lifecycle plugins receive the
    // corresponding host event from the application shell.
    static void Step(World& world, float dt);
};

} // namespace ce::engine
