#pragma once

#include <cstdint>
#include <functional>

namespace ce::engine { class World; }
namespace ce::physics { class PhysicsWorld; }
namespace ce::frust { class EngineFrustHost; }
namespace ce::input { class InputActionSystem; }

namespace ce::runtime
{

// The executable surfaces share one authoritative gameplay order. Rendering
// remains outside this class: a surface publishes the completed World when it
// is ready, but cannot quietly invent a different simulation loop.
class RuntimeWorldRunner final
{
public:
    struct Hooks
    {
        std::function<void(float)> afterPhysics;
        std::function<void(float)> afterPostPhysics;
    };

    RuntimeWorldRunner(engine::World& world, physics::PhysicsWorld& physics,
                       frust::EngineFrustHost& frust, input::InputActionSystem& input);

    void BeginPlay();
    void EndPlay();
    [[nodiscard]] bool IsPlaying() const noexcept { return playing_; }

    // Uses a measured elapsed duration supplied by the host. The runner owns
    // no wall clock, which keeps it deterministic in tests and suitable for a
    // dedicated server's scheduler as well as a JUCE timer.
    void RunFrame(float elapsedSeconds, const Hooks& hooks = {});

private:
    engine::World& world_;
    physics::PhysicsWorld& physics_;
    frust::EngineFrustHost& frust_;
    input::InputActionSystem& input_;
    bool playing_ = false;
};

} // namespace ce::runtime
