#pragma once

#include "engine/gameplay_components.h"
#include "engine/world.h"

namespace ce::engine
{

// Deterministic, reusable Foundation Gameplay simulation. It owns no input
// device and no rendering policy; callers provide one command per tick.
class FoundationGameplay
{
public:
    static void Step(World& world, const GameplayInput& input, float dt);
};

} // namespace ce::engine
