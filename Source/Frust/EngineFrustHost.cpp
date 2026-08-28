#include "Frust/EngineFrustHost.h"

#include <juce_core/juce_core.h>

#include "engine/core_components.h"
#include "engine/world.h"

namespace ce::frust
{
EngineFrustHost* EngineFrustHost::activeHost = nullptr;

EngineFrustHost::EngineFrustHost(engine::World& worldToHost)
    : world(worldToHost)
{
    activeHost = this;
    runtime.registerHostFunction("engine_current_tick", reinterpret_cast<void*>(&EngineFrustHost::currentTick));
    runtime.registerHostFunction("engine_first_transform_entity", reinterpret_cast<void*>(&EngineFrustHost::firstTransformEntity));
    runtime.registerHostFunction("engine_set_position_x", reinterpret_cast<void*>(&EngineFrustHost::setPositionX));
}

EngineFrustHost::~EngineFrustHost()
{
    runtime.unload();
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

void EngineFrustHost::dispatch(EngineFrustEvent event, std::int64_t argument)
{
    (void)runtime.callEvent(static_cast<std::int64_t>(event), argument);
}

bool EngineFrustHost::isLoaded() const noexcept
{
    return runtime.isLoaded();
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
} // namespace ce::frust
