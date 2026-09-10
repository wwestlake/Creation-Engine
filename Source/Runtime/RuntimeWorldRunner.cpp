#include "RuntimeWorldRunner.h"

#include "Frust/EngineFrustHost.h"
#include "Input/InputActionSystem.h"
#include "Physics/PhysicsWorld.h"
#include "engine/foundation_gameplay.h"
#include "engine/simulation.h"
#include "engine/world.h"

#include <algorithm>
#include <mutex>

namespace ce::runtime
{

RuntimeWorldRunner::RuntimeWorldRunner(engine::World& world, physics::PhysicsWorld& physics,
                                       frust::EngineFrustHost& frust, input::InputActionSystem& input)
    : world_(world), physics_(physics), frust_(frust), input_(input)
{
}

void RuntimeWorldRunner::BeginPlay()
{
    if (playing_)
        return;
    playing_ = true;
    frust_.beginPlay(static_cast<std::int64_t>(world_.CurrentTick()));
}

void RuntimeWorldRunner::EndPlay()
{
    if (!playing_)
        return;
    frust_.endPlay(static_cast<std::int64_t>(world_.CurrentTick()));
    playing_ = false;
}

void RuntimeWorldRunner::RunFrame(float elapsedSeconds, const Hooks& hooks)
{
    if (!playing_)
        return;

    const float dt = std::clamp(elapsedSeconds, 0.0f, 0.25f);
    input_.PollOncePerFrame();
    engine::Simulation::Step(world_, dt);
    engine::FoundationGameplay::Step(world_, {}, dt);
    frust_.prePhysicsTick(static_cast<std::int64_t>(world_.CurrentTick()));

    const float physicsAlpha = physics_.Advance(world_, dt);
    {
        std::lock_guard<std::mutex> registryLock(world_.RegistryMutex());
        physics_.InterpolateTransforms(world_, physicsAlpha);
    }
    if (hooks.afterPhysics)
        hooks.afterPhysics(dt);

    frust_.postPhysicsTick(static_cast<std::int64_t>(world_.CurrentTick()));
    if (hooks.afterPostPhysics)
        hooks.afterPostPhysics(dt);
}

} // namespace ce::runtime
