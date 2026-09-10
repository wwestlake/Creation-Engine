#include "Runtime/PossessionService.h"

#include "Character/PossessedCharacter.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"
#include "engine/world.h"

#include <entt/entt.hpp>
#include <mutex>

namespace ce::runtime
{
namespace
{
class CharacterPuppetRuntime final : public PuppetRuntime
{
public:
    PuppetSubjectKind subjectKind() const noexcept override { return PuppetSubjectKind::character; }

    bool activate(engine::World& world, physics::PhysicsWorld& physics, const PossessionRequest& request) override
    {
        {
            std::lock_guard<std::mutex> lock(world.RegistryMutex());
            const auto entity = static_cast<entt::entity>(request.subjectEntityId);
            auto& registry = world.Registry();
            // A real CharacterInstanceRef is the admission ticket. This prevents
            // the old fallback behavior from quietly possessing arbitrary meshes.
            if (!registry.valid(entity) || !registry.all_of<scene::CharacterInstanceRef, engine::Transform>(entity))
                return false;
        }
        return physics.CreateCharacter(world, request.subjectEntityId,
                                       request.capsuleRadiusMeters, request.capsuleHalfHeightMeters);
    }

    void deactivate(engine::World&, physics::PhysicsWorld& physics) override
    {
        if (entityId_ != -1)
            physics.DestroyCharacter(entityId_);
        entityId_ = -1;
    }

    void tick(engine::World& world, physics::PhysicsWorld& physics,
              const input::InputActionSystem& input, float forwardYawRadians, float elapsedSeconds) override
    {
        if (entityId_ != -1)
            character_.Update(world, physics, input, entityId_, forwardYawRadians, elapsedSeconds);
    }

    void setEntityId(std::int64_t entityId) { entityId_ = entityId; }

private:
    character::PossessedCharacter character_;
    std::int64_t entityId_ = -1;
};
}

PossessionService::PossessionService(engine::World& world, physics::PhysicsWorld& physics,
                                     input::InputActionSystem& input)
    : world_(world), physics_(physics), input_(input)
{
}

PossessionService::~PossessionService()
{
    release();
}

bool PossessionService::possessCharacter(const PossessionRequest& request, juce::String& error)
{
    if (request.subjectEntityId == -1 || request.subjectInstanceId.isEmpty())
    {
        error = "Possession requires a persisted Character Instance.";
        return false;
    }

    release();
    auto characterRuntime = std::make_unique<CharacterPuppetRuntime>();
    characterRuntime->setEntityId(request.subjectEntityId);
    if (!characterRuntime->activate(world_, physics_, request))
    {
        error = "The selected scene entity is not a valid Character Instance.";
        return false;
    }

    request_ = request;
    runtime_ = std::move(characterRuntime);
    return true;
}

void PossessionService::release()
{
    if (runtime_ != nullptr)
        runtime_->deactivate(world_, physics_);
    runtime_.reset();
    request_ = {};
}

void PossessionService::tick(float forwardYawRadians, float elapsedSeconds)
{
    if (runtime_ != nullptr)
        runtime_->tick(world_, physics_, input_, forwardYawRadians, elapsedSeconds);
}

} // namespace ce::runtime
