#pragma once

#include <memory>

#include "Runtime/PuppetRuntime.h"

namespace ce::runtime
{

// One bounded controller-to-subject relationship. A service instance belongs
// to one World, so editor play, standalone clients, and later servers all
// execute the same possession lifetime rules.
class PossessionService final
{
public:
    PossessionService(engine::World& world, physics::PhysicsWorld& physics, input::InputActionSystem& input);
    ~PossessionService();

    PossessionService(const PossessionService&) = delete;
    PossessionService& operator=(const PossessionService&) = delete;

    [[nodiscard]] bool possessCharacter(const PossessionRequest& request, juce::String& error);
    void release();
    void tick(float forwardYawRadians, float elapsedSeconds);

    [[nodiscard]] bool isPossessing() const noexcept { return runtime_ != nullptr; }
    [[nodiscard]] std::int64_t subjectEntityId() const noexcept { return request_.subjectEntityId; }
    [[nodiscard]] const PossessionRequest& activeRequest() const noexcept { return request_; }

private:
    engine::World& world_;
    physics::PhysicsWorld& physics_;
    input::InputActionSystem& input_;
    PossessionRequest request_;
    std::unique_ptr<PuppetRuntime> runtime_;
};

} // namespace ce::runtime
