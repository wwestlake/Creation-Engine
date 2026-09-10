#include "Runtime/CameraDirector.h"

#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"
#include "engine/world.h"

#include <entt/entt.hpp>
#include <mutex>

namespace ce::runtime
{
void CameraDirector::attach(std::int64_t subjectEntityId) { subjectEntityId_ = subjectEntityId; detachedPoseCaptured_ = false; }
void CameraDirector::detach() { subjectEntityId_ = -1; detachedPoseCaptured_ = false; }
void CameraDirector::setMode(CameraMode mode) { if (mode_ != mode) { mode_ = mode; detachedPoseCaptured_ = false; } }
void CameraDirector::cycleMode()
{
    switch (mode_) {
        case CameraMode::follow: setMode(CameraMode::firstPerson); break;
        case CameraMode::firstPerson: setMode(CameraMode::topDown); break;
        case CameraMode::topDown: setMode(CameraMode::detachedFly); break;
        case CameraMode::detachedFly: setMode(CameraMode::follow); break;
    }
}

bool CameraDirector::update(engine::World& world, physics::PhysicsWorld& physics,
                            juce::Vector3D<float> forward, juce::Vector3D<float>& position,
                            juce::Vector3D<float>& target)
{
    if (subjectEntityId_ == -1) return false;
    engine::Vec3 feet;
    {
        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        const auto entity = static_cast<entt::entity>(subjectEntityId_);
        if (!world.Registry().valid(entity) || !world.Registry().all_of<engine::Transform>(entity)) return false;
        feet = world.Registry().get<engine::Transform>(entity).position;
    }
    const juce::Vector3D<float> pivot{ feet.x, feet.y + 1.55f, feet.z };
    const auto flat = juce::Vector3D<float>{ forward.x, 0.0f, forward.z }.normalised();
    if (mode_ == CameraMode::firstPerson) { position = pivot; target = pivot + forward; return true; }
    if (mode_ == CameraMode::detachedFly) {
        if (!detachedPoseCaptured_) { detachedPosition_ = pivot - flat * 4.0f + juce::Vector3D<float>{ 0, 2.0f, 0 }; detachedPoseCaptured_ = true; }
        position = detachedPosition_; target = pivot; return true;
    }
    if (mode_ == CameraMode::topDown) { position = pivot - flat * 3.0f + juce::Vector3D<float>{ 0, 12.0f, 0 }; target = pivot; return true; }
    const auto desired = pivot - flat * 4.0f + juce::Vector3D<float>{ 0, 1.5f, 0 };
    const auto direction = desired - pivot;
    const auto distance = direction.length();
    const auto hit = physics.CastRay(pivot.x, pivot.y, pivot.z, direction.x, direction.y, direction.z, distance);
    // A subject must never occlude its own follow camera. Keep a usable
    // third-person boom even when a query begins inside incidental nearby
    // geometry; substantial world obstructions still shorten it normally.
    constexpr float kMinimumFollowDistance = 1.5f;
    constexpr float kCollisionPadding = 0.15f;
    const bool isSubjectHit = hit.hitEntity == subjectEntityId_;
    const bool useCollision = hit.hit && !isSubjectHit && hit.distance > kCollisionPadding;
    const float followDistance = useCollision
        ? juce::jmax(kMinimumFollowDistance, hit.distance - kCollisionPadding)
        : distance;
    position = pivot + direction.normalised() * followDistance;
    target = pivot;
    return true;
}
} // namespace ce::runtime
