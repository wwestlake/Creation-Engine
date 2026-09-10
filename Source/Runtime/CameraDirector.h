#pragma once

#include <cstdint>

#include <JuceHeader.h>

namespace ce::engine { class World; }
namespace ce::physics { class PhysicsWorld; }

namespace ce::runtime
{
enum class CameraMode { firstPerson, follow, topDown, detachedFly };

// A camera observes a Puppet subject; it never changes the character or
// possession. Rendering hosts apply the returned pose to their own camera.
class CameraDirector final
{
public:
    void attach(std::int64_t subjectEntityId);
    void detach();
    void setMode(CameraMode mode);
    void cycleMode();
    [[nodiscard]] CameraMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool isAttached() const noexcept { return subjectEntityId_ != -1; }
    [[nodiscard]] bool update(engine::World& world, physics::PhysicsWorld& physics,
                              juce::Vector3D<float> forward, juce::Vector3D<float>& position,
                              juce::Vector3D<float>& target);

private:
    std::int64_t subjectEntityId_ = -1;
    // Play begins in an observable third-person view. First-person remains
    // an available mode, but should never be the surprise default while an
    // author is first verifying a placed character in the editor.
    CameraMode mode_ = CameraMode::follow;
    bool detachedPoseCaptured_ = false;
    juce::Vector3D<float> detachedPosition_{};
};
} // namespace ce::runtime
