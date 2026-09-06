#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <cstdio>
#include <thread>

// Phase 1 of the Jolt vendoring plan: prove Jolt links and a PhysicsSystem
// can actually be constructed and stepped, in isolation, before any engine
// wrapper code depends on it. Deliberately the smallest possible correct
// Jolt setup -- no bodies, no shapes, just Init + a few Update() calls with
// zero contents -- mirroring Jolt's own HelloWorld.cpp sample structure for
// the required broadphase/object-layer interfaces (PhysicsSystem::Init has
// no default/no-op implementation for these; a minimal real one is
// required even for an empty world).

namespace {
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
};
} // namespace

int main() {
    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    JPH::TempAllocatorImpl tempAllocator(10 * 1024 * 1024);
    JPH::JobSystemThreadPool jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>(std::thread::hardware_concurrency()) - 1);

    BPLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;

    JPH::PhysicsSystem physicsSystem;
    physicsSystem.Init(
        /* inMaxBodies */ 64,
        /* inNumBodyMutexes */ 0,
        /* inMaxBodyPairs */ 64,
        /* inMaxContactConstraints */ 64,
        broadPhaseLayerInterface,
        objectVsBroadPhaseLayerFilter,
        objectLayerPairFilter);

    for (int i = 0; i < 10; ++i) {
        physicsSystem.Update(1.0f / 60.0f, /* inCollisionSteps */ 1, &tempAllocator, &jobSystem);
    }

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    std::printf("Jolt link/step smoke check passed: linked, initialized, stepped 10x with an empty world.\n");
    return 0;
}
