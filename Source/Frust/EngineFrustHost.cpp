#include "Frust/EngineFrustHost.h"

#include <juce_core/juce_core.h>

#include "engine/core_components.h"
#include "engine/world.h"
#include "Scene/Components.h"

namespace ce::frust
{
EngineFrustHost* EngineFrustHost::activeHost = nullptr;

EngineFrustHost::EngineFrustHost(engine::World& worldToHost)
    : world(worldToHost)
{
    activeHost = this;
    runtime.registerHostFunction("engine_current_tick", reinterpret_cast<void*>(&EngineFrustHost::currentTick));
    runtime.registerHostFunction("engine_first_transform_entity", reinterpret_cast<void*>(&EngineFrustHost::firstTransformEntity));
    runtime.registerHostFunction("engine_current_object_entity", reinterpret_cast<void*>(&EngineFrustHost::currentObjectEntity));
    runtime.registerHostFunction("engine_set_position_x", reinterpret_cast<void*>(&EngineFrustHost::setPositionX));
}

EngineFrustHost::~EngineFrustHost()
{
    runtime.unloadAll();
    if (activeHost == this) {
        activeHost = nullptr;
    }
}

bool EngineFrustHost::load(const std::string& pluginPath, std::string& error)
{
    return runtime.load(pluginPath, error);
}

bool EngineFrustHost::loadBundled(std::string& error)
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto pluginFile = executable.getParentDirectory().getChildFile("plugins").getChildFile("EngineLifecycle.frust");
    if (!pluginFile.existsAsFile()) {
        error = "Bundled EngineLifecycle.frust plugin was not found beside the executable.";
        return false;
    }

    return load(pluginFile.getFullPathName().toStdString(), error);
}

bool EngineFrustHost::loadObjectBehavior(const std::string& podId, const std::string& pluginPath, std::string& error)
{
    if (podId.empty()) {
        error = "An object behavior needs a non-empty pod identifier.";
        return false;
    }
    return runtime.load(behaviorKey(podId), pluginPath, error);
}

void EngineFrustHost::dispatch(EngineFrustEvent event, std::int64_t argument)
{
    (void)runtime.callEvent(static_cast<std::int64_t>(event), argument);
    dispatchObjectBehaviors(event, argument);
}

bool EngineFrustHost::isLoaded() const noexcept
{
    return runtime.isLoaded();
}

bool EngineFrustHost::isObjectBehaviorLoaded(const std::string& podId) const noexcept
{
    return runtime.isLoaded(behaviorKey(podId));
}

std::int64_t EngineFrustHost::currentTick()
{
    return activeHost != nullptr ? static_cast<std::int64_t>(activeHost->world.CurrentTick()) : 0;
}

std::int64_t EngineFrustHost::firstTransformEntity()
{
    if (activeHost == nullptr) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    const auto view = activeHost->world.Registry().view<engine::Transform>();
    const auto first = view.begin();
    if (first == view.end()) {
        return -1;
    }
    return static_cast<std::int64_t>(entt::to_integral(*first));
}

std::int64_t EngineFrustHost::currentObjectEntity()
{
    return activeHost != nullptr ? activeHost->activeObjectEntityId : -1;
}

std::int64_t EngineFrustHost::setPositionX(std::int64_t entityId, std::int64_t positionX)
{
    if (activeHost == nullptr || entityId < 0) {
        return 0;
    }

    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (!registry.valid(entity) || !registry.all_of<engine::Transform>(entity)) {
        return 0;
    }

    registry.get<engine::Transform>(entity).position.x = static_cast<float>(positionX);
    return 1;
}

std::string EngineFrustHost::behaviorKey(const std::string& podId)
{
    return "object-behavior:" + podId;
}

void EngineFrustHost::dispatchObjectBehaviors(EngineFrustEvent event, std::int64_t argument)
{
    struct PendingInvocation
    {
        entt::entity entity = entt::null;
        std::string podId;
    };

    std::vector<PendingInvocation> pending;
    {
        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        const auto objects = world.Registry().view<const scene::BehaviorAttachments>();
        for (const auto entity : objects)
        {
            const auto& attachments = objects.get<const scene::BehaviorAttachments>(entity);
            for (const auto& podId : attachments.podIds) {
                pending.push_back({ entity, podId.toStdString() });
            }
        }
    }

    for (const auto& invocation : pending)
    {
        const auto key = behaviorKey(invocation.podId);
        if (!runtime.isLoaded(key)) {
            continue;
        }

        activeObjectEntityId = static_cast<std::int64_t>(entt::to_integral(invocation.entity));
        (void)runtime.callEvent(key, static_cast<std::int64_t>(event), argument);
    }
    activeObjectEntityId = -1;
}
} // namespace ce::frust
