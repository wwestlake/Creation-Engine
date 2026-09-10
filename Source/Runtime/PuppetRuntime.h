#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

namespace ce::engine { class World; }
namespace ce::physics { class PhysicsWorld; }
namespace ce::input { class InputActionSystem; }

namespace ce::runtime
{

enum class PuppetSubjectKind { character, robot, animal, vehicle, drone, turret };

struct PossessionRequest final
{
    juce::String controllerId;
    juce::String inputContext;
    juce::String authority;
    juce::String subjectInstanceId;
    std::int64_t subjectEntityId = -1;
    float capsuleRadiusMeters = 0.3f;
    float capsuleHalfHeightMeters = 0.9f;
};

// Common runtime contract for every thing a controller may possess. It keeps
// camera code and possession lifetime independent from a humanoid controller.
class PuppetRuntime
{
public:
    virtual ~PuppetRuntime() = default;
    virtual PuppetSubjectKind subjectKind() const noexcept = 0;
    virtual bool activate(engine::World& world, physics::PhysicsWorld& physics, const PossessionRequest& request) = 0;
    virtual void deactivate(engine::World& world, physics::PhysicsWorld& physics) = 0;
    virtual void tick(engine::World& world, physics::PhysicsWorld& physics,
                      const input::InputActionSystem& input, float forwardYawRadians, float elapsedSeconds) = 0;
};

} // namespace ce::runtime
