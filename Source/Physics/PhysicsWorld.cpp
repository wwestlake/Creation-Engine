#include "PhysicsWorld.h"

#include "engine/core_components.h"
#include "engine/world.h"

#include <entt/entt.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace ce::physics
{
namespace
{
// Two broadphase/object layers -- matches the smoke test's own minimal
// setup (PhysicsJoltLinkSmoke.cpp). Static bodies never move so they never
// need to be found by a moving-object broadphase sweep; that's the whole
// reason engines split these two layers rather than using one for
// everything.
namespace Layers
{
    constexpr JPH::ObjectLayer NON_MOVING = 0;
    constexpr JPH::ObjectLayer MOVING = 1;
    constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}
namespace BroadPhaseLayers
{
    const JPH::BroadPhaseLayer NON_MOVING(0);
    const JPH::BroadPhaseLayer MOVING(1);
    constexpr JPH::uint NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        objectToBroadPhase_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        objectToBroadPhase_[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        return objectToBroadPhase_[inLayer];
    }

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
};

constexpr float kFixedStepSeconds = 1.0f / 60.0f;
constexpr float kMaxAccumulatedSeconds = 0.25f; // avoids a spiral of death after a long stall.

JPH::RefConst<JPH::Shape> MakeShape(const ColliderComponent& collider)
{
    switch (collider.shape)
    {
    case ColliderShapeKind::Sphere:
        return JPH::SphereShapeSettings(collider.radius).Create().Get();
    case ColliderShapeKind::Capsule:
        return JPH::CapsuleShapeSettings(collider.halfHeight, collider.radius).Create().Get();
    case ColliderShapeKind::Box:
    default:
        return JPH::BoxShapeSettings(
            JPH::Vec3(collider.halfExtentX, collider.halfExtentY, collider.halfExtentZ)).Create().Get();
    }
}

JPH::EMotionType ToJoltMotionType(MotionType motionType)
{
    switch (motionType)
    {
    case MotionType::Static: return JPH::EMotionType::Static;
    case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:
    default: return JPH::EMotionType::Dynamic;
    }
}
} // namespace

struct PhysicsWorld::Impl final : public JPH::ContactListener
{
    Impl()
        : jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                    std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1))
        , tempAllocator(10 * 1024 * 1024)
    {
        physicsSystem.Init(
            /* inMaxBodies */ 4096,
            /* inNumBodyMutexes */ 0,
            /* inMaxBodyPairs */ 4096,
            /* inMaxContactConstraints */ 4096,
            broadPhaseLayerInterface,
            objectVsBroadPhaseLayerFilter,
            objectLayerPairFilter);
        physicsSystem.SetContactListener(this);
    }

    // --- JPH::ContactListener -----------------------------------------
    // Contact callbacks fire from Jolt's own job threads with all bodies
    // locked (see ContactListener.h's class comment) -- no locking calls
    // allowed here, just queue the event under our own small mutex for
    // DrainCollisionEvents() to hand to EngineFrustHost::tick() later.
    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override
    {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold&,
                        JPH::ContactSettings&) override
    {
        QueueEvent(CollisionEvent::Kind::Begin, body1.GetID(), body2.GetID());
    }

    void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold&,
                            JPH::ContactSettings&) override
    {
        QueueEvent(CollisionEvent::Kind::Persist, body1.GetID(), body2.GetID());
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override
    {
        // No live Body& here (the body may already be destroyed by the time
        // this fires, see ContactListener.h) -- entity ids must come from
        // our own bodyToEntity map, populated/erased at create/destroy
        // time, not from Jolt's own user-data lookup.
        QueueEvent(CollisionEvent::Kind::End, subShapePair.GetBody1ID(), subShapePair.GetBody2ID());
    }

    void QueueEvent(CollisionEvent::Kind kind, JPH::BodyID bodyId1, JPH::BodyID bodyId2)
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        const auto entity1 = bodyToEntity.find(bodyId1);
        const auto entity2 = bodyToEntity.find(bodyId2);
        CollisionEvent event;
        event.kind = kind;
        event.entityA = entity1 != bodyToEntity.end() ? static_cast<std::int64_t>(entt::to_integral(entity1->second)) : -1;
        event.entityB = entity2 != bodyToEntity.end() ? static_cast<std::int64_t>(entt::to_integral(entity2->second)) : -1;
        pendingEvents.push_back(event);
    }

    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;
    JPH::JobSystemThreadPool jobSystem;
    JPH::TempAllocatorImpl tempAllocator;
    JPH::PhysicsSystem physicsSystem;

    float accumulatorSeconds = 0.0f;
    std::unordered_map<JPH::BodyID, entt::entity> bodyToEntity;
    std::mutex eventMutex;
    std::vector<CollisionEvent> pendingEvents;

    // Possessable Designer Character plan, Phase 1 -- deliberately separate
    // from bodyToEntity/RigidBodyComponent above: a character is not a
    // rigid body, it's a manually-driven kinematic controller Jolt itself
    // does not track.
    std::unordered_map<entt::entity, JPH::Ref<JPH::CharacterVirtual>> characters;
    // CharacterVirtual has its own separate contact system
    // (GetActiveContacts()), NOT the rigid-body ContactListener above --
    // this tracks which bodies each character was touching last tick, so
    // Begin/End can be derived and pushed into the SAME pendingEvents queue
    // rigid-body contacts use, so a Pod reacts identically either way.
    std::unordered_map<entt::entity, std::unordered_set<JPH::BodyID>> characterContactsLastTick;
};

PhysicsWorld::PhysicsWorld()
{
    // Jolt's global registration is process-wide, not per-PhysicsWorld --
    // guarded so a test creating more than one PhysicsWorld in the same
    // process (unusual, but smoke tests do exactly this pattern) doesn't
    // double-register.
    static bool globalsRegistered = false;
    if (!globalsRegistered)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        globalsRegistered = true;
    }

    impl_ = std::make_unique<Impl>();
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::AttachToWorld(ce::engine::World& world)
{
    // Dangling-handle safety (Decision 6): an entity's RigidBodyComponent
    // can be removed by ANY path (explicit component removal, or the whole
    // entity being destroyed) -- entt's on_destroy fires for all of them,
    // unlike a single call site like notifyObjectDestroyed would. Removes
    // and destroys the Jolt body immediately so a gone entity never leaves
    // a "ghost" body still simulating.
    world.Registry().on_destroy<RigidBodyComponent>().connect<[](entt::registry& registry, entt::entity entity)
    {
        // Static connect-time lambda can't capture `this` -- reads the
        // owning PhysicsWorld::Impl back out of the registry's own
        // context, stashed there once below.
        auto* impl = registry.ctx().find<PhysicsWorld::Impl*>();
        if (impl == nullptr || *impl == nullptr)
            return;
        auto& rigidBody = registry.get<RigidBodyComponent>(entity);
        if (rigidBody.jphBody.IsInvalid())
            return;
        auto& bodyInterface = (*impl)->physicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(rigidBody.jphBody);
        bodyInterface.DestroyBody(rigidBody.jphBody);
        {
            std::lock_guard<std::mutex> lock((*impl)->eventMutex);
            (*impl)->bodyToEntity.erase(rigidBody.jphBody);
        }
    }>();

    // Same safety, for characters (Possessable Designer Character plan,
    // Phase 1) -- a separate map, not a component field, since
    // CharacterVirtual is heavier than a BodyID (see CharacterControllerTag's
    // own comment).
    world.Registry().on_destroy<CharacterControllerTag>().connect<[](entt::registry& registry, entt::entity entity)
    {
        auto* impl = registry.ctx().find<PhysicsWorld::Impl*>();
        if (impl != nullptr && *impl != nullptr)
        {
            (*impl)->characters.erase(entity);
            std::lock_guard<std::mutex> eventLock((*impl)->eventMutex);
            (*impl)->characterContactsLastTick.erase(entity);
        }
    }>();

    world.Registry().ctx().emplace<PhysicsWorld::Impl*>(impl_.get());
}

float PhysicsWorld::Advance(ce::engine::World& world, float realElapsedSeconds)
{
    if (realElapsedSeconds <= 0.0f)
        return impl_->accumulatorSeconds / kFixedStepSeconds;

    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    auto& registry = world.Registry();
    auto& bodyInterface = impl_->physicsSystem.GetBodyInterface();

    // --- Reconciliation pass (Decision 3): create the real Jolt body for
    // any entity that has both config components but no body yet. ---
    {
        auto view = registry.view<RigidBodyComponent, ColliderComponent, ce::engine::Transform>();
        for (const auto entity : view)
        {
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            if (!rigidBody.jphBody.IsInvalid())
                continue;

            const auto& collider = view.get<ColliderComponent>(entity);
            const auto& transform = view.get<ce::engine::Transform>(entity);
            const auto shape = MakeShape(collider);
            const JPH::ObjectLayer objectLayer =
                rigidBody.motionType == MotionType::Static ? Layers::NON_MOVING : Layers::MOVING;

            JPH::BodyCreationSettings settings(
                shape.GetPtr(),
                JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
                JPH::Quat::sEulerAngles(JPH::Vec3(transform.eulerRotationRadians.x,
                                                  transform.eulerRotationRadians.y,
                                                  transform.eulerRotationRadians.z)),
                ToJoltMotionType(rigidBody.motionType),
                objectLayer);
            settings.mFriction = rigidBody.friction;
            settings.mRestitution = rigidBody.restitution;
            settings.mLinearDamping = rigidBody.linearDamping;
            settings.mAngularDamping = rigidBody.angularDamping;
            settings.mUserData = static_cast<JPH::uint64>(entt::to_integral(entity));
            settings.mIsSensor = collider.isSensor;
            if (rigidBody.motionType == MotionType::Dynamic)
            {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = rigidBody.mass;
            }

            const auto activation = rigidBody.motionType == MotionType::Static
                ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
            rigidBody.jphBody = bodyInterface.CreateAndAddBody(settings, activation);

            {
                std::lock_guard<std::mutex> eventLock(impl_->eventMutex);
                impl_->bodyToEntity[rigidBody.jphBody] = entity;
            }

            if (!registry.all_of<PhysicsRenderState>(entity))
            {
                auto& renderState = registry.emplace<PhysicsRenderState>(entity);
                renderState.previousX = renderState.currentX = transform.position.x;
                renderState.previousY = renderState.currentY = transform.position.y;
                renderState.previousZ = renderState.currentZ = transform.position.z;
                renderState.previousRotX = renderState.currentRotX = transform.eulerRotationRadians.x;
                renderState.previousRotY = renderState.currentRotY = transform.eulerRotationRadians.y;
                renderState.previousRotZ = renderState.currentRotZ = transform.eulerRotationRadians.z;
            }
        }
    }

    // --- Pre-tick: kinematic bodies are driven externally -- always push
    // whatever Transform currently says into Jolt (Core Architectural
    // Invariant 1: entt owns the hierarchy, Jolt just simulates). ---
    {
        auto view = registry.view<RigidBodyComponent, ce::engine::Transform>();
        for (const auto entity : view)
        {
            const auto& rigidBody = view.get<RigidBodyComponent>(entity);
            if (rigidBody.motionType != MotionType::Kinematic || rigidBody.jphBody.IsInvalid())
                continue;
            const auto& transform = view.get<ce::engine::Transform>(entity);
            bodyInterface.SetPositionAndRotation(
                rigidBody.jphBody,
                JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
                JPH::Quat::sEulerAngles(JPH::Vec3(transform.eulerRotationRadians.x,
                                                  transform.eulerRotationRadians.y,
                                                  transform.eulerRotationRadians.z)),
                JPH::EActivation::Activate);
        }
    }

    // --- Fixed-step accumulator (Core Architectural Invariant 2). ---
    impl_->accumulatorSeconds = std::min(impl_->accumulatorSeconds + realElapsedSeconds, kMaxAccumulatedSeconds);
    auto dynamicView = registry.view<RigidBodyComponent, PhysicsRenderState>();
    while (impl_->accumulatorSeconds >= kFixedStepSeconds)
    {
        // Shift current -> previous for every simulated body before this
        // step, so InterpolateTransforms() always blends "last step's
        // result" -> "this step's result," regardless of how many fixed
        // steps ran inside this one Advance() call.
        for (const auto entity : dynamicView)
        {
            auto& renderState = dynamicView.get<PhysicsRenderState>(entity);
            renderState.previousX = renderState.currentX;
            renderState.previousY = renderState.currentY;
            renderState.previousZ = renderState.currentZ;
            renderState.previousRotX = renderState.currentRotX;
            renderState.previousRotY = renderState.currentRotY;
            renderState.previousRotZ = renderState.currentRotZ;
        }

        impl_->physicsSystem.Update(kFixedStepSeconds, 1, &impl_->tempAllocator, &impl_->jobSystem);
        impl_->accumulatorSeconds -= kFixedStepSeconds;

        for (const auto entity : dynamicView)
        {
            auto& rigidBody = dynamicView.get<RigidBodyComponent>(entity);
            auto& renderState = dynamicView.get<PhysicsRenderState>(entity);
            if (rigidBody.jphBody.IsInvalid())
                continue;
            JPH::RVec3 position; JPH::Quat rotation;
            bodyInterface.GetPositionAndRotation(rigidBody.jphBody, position, rotation);
            const auto euler = rotation.GetEulerAngles();
            renderState.currentX = static_cast<float>(position.GetX());
            renderState.currentY = static_cast<float>(position.GetY());
            renderState.currentZ = static_cast<float>(position.GetZ());
            renderState.currentRotX = euler.GetX();
            renderState.currentRotY = euler.GetY();
            renderState.currentRotZ = euler.GetZ();
        }
    }

    return impl_->accumulatorSeconds / kFixedStepSeconds;
}

void PhysicsWorld::InterpolateTransforms(ce::engine::World& world, float alpha) const
{
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    auto view = world.Registry().view<PhysicsRenderState, ce::engine::Transform>();
    for (const auto entity : view)
    {
        const auto& renderState = view.get<PhysicsRenderState>(entity);
        auto& transform = view.get<ce::engine::Transform>(entity);
        transform.position.x = renderState.previousX + (renderState.currentX - renderState.previousX) * alpha;
        transform.position.y = renderState.previousY + (renderState.currentY - renderState.previousY) * alpha;
        transform.position.z = renderState.previousZ + (renderState.currentZ - renderState.previousZ) * alpha;
        transform.eulerRotationRadians.x = renderState.previousRotX + (renderState.currentRotX - renderState.previousRotX) * alpha;
        transform.eulerRotationRadians.y = renderState.previousRotY + (renderState.currentRotY - renderState.previousRotY) * alpha;
        transform.eulerRotationRadians.z = renderState.previousRotZ + (renderState.currentRotZ - renderState.previousRotZ) * alpha;
    }
}

std::vector<CollisionEvent> PhysicsWorld::DrainCollisionEvents()
{
    std::lock_guard<std::mutex> lock(impl_->eventMutex);
    std::vector<CollisionEvent> drained;
    drained.swap(impl_->pendingEvents);
    return drained;
}

RaycastHit PhysicsWorld::CastRay(float originX, float originY, float originZ,
                                 float dirX, float dirY, float dirZ, float maxDistance)
{
    RaycastHit result;
    const JPH::RVec3 origin(originX, originY, originZ);
    const JPH::Vec3 direction = JPH::Vec3(dirX, dirY, dirZ).NormalizedOr(JPH::Vec3::sZero()) * maxDistance;
    const JPH::RRayCast ray(origin, direction);

    JPH::RayCastResult hit;
    if (!impl_->physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit))
        return result;

    result.hit = true;
    result.distance = hit.mFraction * maxDistance;

    {
        std::lock_guard<std::mutex> lock(impl_->eventMutex);
        const auto found = impl_->bodyToEntity.find(hit.mBodyID);
        result.hitEntity = found != impl_->bodyToEntity.end()
            ? static_cast<std::int64_t>(entt::to_integral(found->second)) : -1;
    }

    JPH::BodyLockRead bodyLock(impl_->physicsSystem.GetBodyLockInterface(), hit.mBodyID);
    if (bodyLock.Succeeded())
    {
        const auto hitPosition = ray.GetPointOnRay(hit.mFraction);
        const auto normal = bodyLock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPosition);
        result.normalX = normal.GetX();
        result.normalY = normal.GetY();
        result.normalZ = normal.GetZ();
    }

    return result;
}

namespace
{
// Shared lookup used by every per-entity method below: validates the
// entity id and returns its live JPH::BodyID, or an invalid BodyID if the
// entity doesn't exist, has no RigidBodyComponent, or its body hasn't been
// created yet by the reconciliation pass in Advance().
JPH::BodyID FindBodyId(ce::engine::World& world, std::int64_t entity)
{
    const auto entityHandle = static_cast<entt::entity>(entity);
    auto& registry = world.Registry();
    if (!registry.valid(entityHandle))
        return {};
    if (const auto* rigidBody = registry.try_get<RigidBodyComponent>(entityHandle))
        return rigidBody->jphBody;
    return {};
}
} // namespace

void PhysicsWorld::ApplyForce(ce::engine::World& world, std::int64_t entity, float x, float y, float z)
{
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const auto bodyId = FindBodyId(world, entity);
    if (bodyId.IsInvalid())
        return;
    impl_->physicsSystem.GetBodyInterface().AddForce(bodyId, JPH::Vec3(x, y, z));
}

void PhysicsWorld::ApplyImpulse(ce::engine::World& world, std::int64_t entity, float x, float y, float z)
{
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const auto bodyId = FindBodyId(world, entity);
    if (bodyId.IsInvalid())
        return;
    impl_->physicsSystem.GetBodyInterface().AddImpulse(bodyId, JPH::Vec3(x, y, z));
}

void PhysicsWorld::SetLinearVelocity(ce::engine::World& world, std::int64_t entity, float x, float y, float z)
{
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const auto bodyId = FindBodyId(world, entity);
    if (bodyId.IsInvalid())
        return;
    impl_->physicsSystem.GetBodyInterface().SetLinearVelocity(bodyId, JPH::Vec3(x, y, z));
}

void PhysicsWorld::GetLinearVelocity(ce::engine::World& world, std::int64_t entity, float& outX, float& outY, float& outZ) const
{
    outX = outY = outZ = 0.0f;
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const auto bodyId = FindBodyId(world, entity);
    if (bodyId.IsInvalid())
        return;
    const auto velocity = impl_->physicsSystem.GetBodyInterface().GetLinearVelocity(bodyId);
    outX = velocity.GetX();
    outY = velocity.GetY();
    outZ = velocity.GetZ();
}

bool PhysicsWorld::CreateCharacter(ce::engine::World& world, std::int64_t entity,
                                   float capsuleRadius, float capsuleHalfHeight,
                                   float maxSlopeAngleDegrees)
{
    const auto entityHandle = static_cast<entt::entity>(entity);
    if (impl_->characters.contains(entityHandle))
        return false;

    JPH::RVec3 startPosition(0, 0, 0);
    {
        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        auto& registry = world.Registry();
        if (!registry.valid(entityHandle))
            return false;
        if (const auto* transform = registry.try_get<ce::engine::Transform>(entityHandle))
            startPosition = JPH::RVec3(transform->position.x, transform->position.y, transform->position.z);
        registry.emplace_or_replace<CharacterControllerTag>(entityHandle);
    }

    // Jolt's own documented convention: "make sure the shape is made so that
    // the bottom of the shape is at (0, 0, 0)" -- a CapsuleShape is centered
    // on its own origin, so it's shifted up by its own half-extent
    // (halfHeight + radius) via RotatedTranslatedShape.
    const auto capsule = JPH::CapsuleShapeSettings(capsuleHalfHeight, capsuleRadius).Create().Get();
    const auto offsetCapsule = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0, capsuleHalfHeight + capsuleRadius, 0), JPH::Quat::sIdentity(), capsule).Create().Get();

    JPH::CharacterVirtualSettings settings;
    settings.mShape = offsetCapsule;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(maxSlopeAngleDegrees);

    auto character = new JPH::CharacterVirtual(&settings, startPosition, JPH::Quat::sIdentity(),
                                               static_cast<JPH::uint64>(entt::to_integral(entityHandle)),
                                               &impl_->physicsSystem);
    impl_->characters[entityHandle] = character;
    return true;
}

void PhysicsWorld::DestroyCharacter(std::int64_t entity)
{
    const auto entityHandle = static_cast<entt::entity>(entity);
    impl_->characters.erase(entityHandle);
    std::lock_guard<std::mutex> lock(impl_->eventMutex);
    impl_->characterContactsLastTick.erase(entityHandle);
}

void PhysicsWorld::UpdateCharacter(ce::engine::World& world, std::int64_t entity,
                                   float desiredHorizontalX, float desiredHorizontalZ,
                                   float jumpSpeed, float dt)
{
    const auto entityHandle = static_cast<entt::entity>(entity);
    const auto found = impl_->characters.find(entityHandle);
    if (found == impl_->characters.end() || dt <= 0.0f)
        return;
    auto& character = *found->second;

    const JPH::Vec3 gravity(0.0f, -9.81f, 0.0f);
    const JPH::Vec3 horizontalIntent(desiredHorizontalX, 0.0f, desiredHorizontalZ);
    JPH::Vec3 newVelocity;
    if (character.IsSupported())
    {
        // Matches Jolt's own documented ExtendedUpdate usage pattern exactly
        // (CharacterVirtual.h's comment above the function): ground velocity
        // (so the character rides moving platforms correctly) plus player
        // intent plus optional jump plus one tick of gravity.
        newVelocity = character.GetGroundVelocity() + horizontalIntent + dt * gravity;
        if (jumpSpeed > 0.0f)
            newVelocity += JPH::Vec3(0.0f, jumpSpeed, 0.0f);
    }
    else
    {
        // Airborne: keep the vertical velocity Jolt is already tracking
        // (falling/jump arc), replace horizontal with fresh player intent.
        const auto currentVelocity = character.GetLinearVelocity();
        newVelocity = JPH::Vec3(desiredHorizontalX, currentVelocity.GetY(), desiredHorizontalZ) + dt * gravity;
    }
    character.SetLinearVelocity(newVelocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    character.ExtendedUpdate(dt, gravity, updateSettings,
                             JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{},
                             JPH::BodyFilter{}, JPH::ShapeFilter{}, impl_->tempAllocator);

    // Derive Begin/End collision events for this character from Jolt's own
    // GetActiveContacts() (its separate contact system -- see
    // characterContactsLastTick's comment) and push them into the same
    // queue rigid-body contacts use, so a Pod reacts identically either way.
    {
        std::unordered_set<JPH::BodyID> currentContacts;
        for (const auto& contact : character.GetActiveContacts())
            if (contact.mHadCollision)
                currentContacts.insert(contact.mBodyB);

        std::lock_guard<std::mutex> eventLock(impl_->eventMutex);
        auto& lastContacts = impl_->characterContactsLastTick[entityHandle];
        const auto otherEntityFor = [this](const JPH::BodyID& bodyId) -> std::int64_t
        {
            const auto found = impl_->bodyToEntity.find(bodyId);
            return found != impl_->bodyToEntity.end() ? static_cast<std::int64_t>(entt::to_integral(found->second)) : -1;
        };
        for (const auto& bodyId : currentContacts)
        {
            if (!lastContacts.contains(bodyId))
            {
                CollisionEvent event;
                event.kind = CollisionEvent::Kind::Begin;
                event.entityA = entity;
                event.entityB = otherEntityFor(bodyId);
                impl_->pendingEvents.push_back(event);
            }
        }
        for (const auto& bodyId : lastContacts)
        {
            if (!currentContacts.contains(bodyId))
            {
                CollisionEvent event;
                event.kind = CollisionEvent::Kind::End;
                event.entityA = entity;
                event.entityB = otherEntityFor(bodyId);
                impl_->pendingEvents.push_back(event);
            }
        }
        lastContacts = std::move(currentContacts);
    }

    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    auto& registry = world.Registry();
    if (registry.valid(entityHandle))
    {
        if (auto* transform = registry.try_get<ce::engine::Transform>(entityHandle))
        {
            const auto position = character.GetPosition();
            transform->position.x = static_cast<float>(position.GetX());
            transform->position.y = static_cast<float>(position.GetY());
            transform->position.z = static_cast<float>(position.GetZ());
        }
    }
}

bool PhysicsWorld::IsCharacterGrounded(std::int64_t entity) const
{
    const auto found = impl_->characters.find(static_cast<entt::entity>(entity));
    return found != impl_->characters.end() && found->second->IsSupported();
}

} // namespace ce::physics
