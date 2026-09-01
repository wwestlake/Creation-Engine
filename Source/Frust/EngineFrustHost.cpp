#include "Frust/EngineFrustHost.h"
#include "Frust/EngineNodeLibraryLoader.h"

#include <algorithm>

#include <juce_core/juce_core.h>

#include "engine/core_components.h"
#include "engine/world.h"
#include "Scene/Components.h"

namespace ce::frust
{
namespace
{
using ObjectLifecycleHook = std::int64_t (*)(std::int64_t);
}

EngineFrustHost* EngineFrustHost::activeHost = nullptr;

EngineFrustHost::EngineFrustHost(engine::World& worldToHost)
    : world(worldToHost)
{
    activeHost = this;
    runtime.registerHostFunction("engine_current_tick", reinterpret_cast<void*>(&EngineFrustHost::currentTick));
    runtime.registerHostFunction("engine_first_transform_entity", reinterpret_cast<void*>(&EngineFrustHost::firstTransformEntity));
    runtime.registerHostFunction("engine_current_object_entity", reinterpret_cast<void*>(&EngineFrustHost::currentObjectEntity));
    runtime.registerHostFunction("engine_set_position_x", reinterpret_cast<void*>(&EngineFrustHost::setPositionX));
    runtime.registerHostFunction("engine_request_scene_transition", reinterpret_cast<void*>(&EngineFrustHost::requestSceneTransition));
    runtime.registerHostFunction("engine_active_game_id", reinterpret_cast<void*>(&EngineFrustHost::activeGameId));
    runtime.registerHostFunction("engine_active_scene_id", reinterpret_cast<void*>(&EngineFrustHost::activeSceneId));
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
    if (!runtime.load(pluginPath, error)) {
        return false;
    }
    if (registerNodeLibraries(creation::frust::PluginRuntime::defaultPluginKey, error)) {
        return true;
    }
    runtime.unload();
    return false;
}

bool EngineFrustHost::loadBundled(std::string& error)
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto plugins = executable.getParentDirectory().getChildFile("plugins");
    const auto pluginFile = plugins.getChildFile("EngineLifecycle.frust");
    const auto coreNodesFile = plugins.getChildFile("CoreNodes.frust");
    if (!pluginFile.existsAsFile() || !coreNodesFile.existsAsFile()) {
        error = "Bundled EngineLifecycle.frust plugin was not found beside the executable.";
        return false;
    }
    if (!load(pluginFile.getFullPathName().toStdString(), error)) return false;
    constexpr const char* coreNodesKey = "built-in:core-nodes";
    if (!runtime.load(coreNodesKey, coreNodesFile.getFullPathName().toStdString(), error)) return false;
    if (registerNodeLibraries(coreNodesKey, error)) return true;
    runtime.unload(coreNodesKey);
    return false;
}

bool EngineFrustHost::loadObjectBehavior(const std::string& podId, const std::string& pluginPath, std::string& error)
{
    if (podId.empty()) {
        error = "An object behavior needs a non-empty pod identifier.";
        return false;
    }
    const auto key = behaviorKey(podId);
    if (!runtime.load(key, pluginPath, error)) {
        return false;
    }
    if (registerNodeLibraries(key, error)) {
        return true;
    }
    runtime.unload(key);
    return false;
}

void EngineFrustHost::dispatch(EngineFrustEvent event, std::int64_t argument)
{
    (void)runtime.callEvent(static_cast<std::int64_t>(event), argument);
}

void EngineFrustHost::prepareLevel(std::int64_t tick)
{
    for (const auto& [entityId, podId] : attachedObjectBehaviors()) {
        ensureObjectLifecycle(entityId, podId, tick);
    }
}

void EngineFrustHost::beginPlay(std::int64_t tick)
{
    if (playActive) {
        return;
    }

    playActive = true;
    dispatch(EngineFrustEvent::simulationStarted, tick);
    prepareLevel(tick);
}

void EngineFrustHost::tick(std::int64_t tick)
{
    if (!playActive) {
        return;
    }

    dispatch(EngineFrustEvent::simulationTick, tick);
    for (const auto& [entityId, podId] : attachedObjectBehaviors())
    {
        ensureObjectLifecycle(entityId, podId, tick);
        if (objectLifecycles[entityId].playingPods.contains(podId)) {
            invokeObjectHook(entityId, podId, "on_tick", EngineFrustEvent::simulationTick, tick);
        }
    }
}

void EngineFrustHost::endPlay(std::int64_t tick)
{
    if (!playActive) {
        return;
    }

    for (const auto& [entityId, podId] : attachedObjectBehaviors())
    {
        const auto found = objectLifecycles.find(entityId);
        if (found != objectLifecycles.end() && found->second.playingPods.erase(podId) != 0) {
            invokeObjectHook(entityId, podId, "on_end_play", EngineFrustEvent::simulationPaused, tick);
        }
    }
    playActive = false;
    dispatch(EngineFrustEvent::simulationPaused, tick);
}

void EngineFrustHost::notifyObjectDestroyed(entt::entity entity, std::int64_t tick)
{
    const auto entityId = static_cast<std::int64_t>(entt::to_integral(entity));
    const auto found = objectLifecycles.find(entityId);
    if (found == objectLifecycles.end()) {
        return;
    }

    for (const auto& podId : found->second.playingPods) {
        invokeObjectHook(entityId, podId, "on_end_play", EngineFrustEvent::simulationPaused, tick);
    }
    for (const auto& podId : found->second.spawnedPods) {
        invokeObjectHook(entityId, podId, "on_destroy", EngineFrustEvent::simulationPaused, tick);
    }
    objectLifecycles.erase(found);
}

void EngineFrustHost::setSceneTransitionRequestHandler(std::function<void(const std::string&)> handler)
{
    sceneTransitionRequestHandler = std::move(handler);
}

void EngineFrustHost::setActiveGameIdProvider(std::function<std::string()> provider)
{
    activeGameIdProvider = std::move(provider);
}

void EngineFrustHost::setActiveSceneIdProvider(std::function<std::string()> provider)
{
    activeSceneIdProvider = std::move(provider);
}

bool EngineFrustHost::isLoaded() const noexcept
{
    return runtime.isLoaded();
}

bool EngineFrustHost::isObjectBehaviorLoaded(const std::string& podId) const noexcept
{
    return runtime.isLoaded(behaviorKey(podId));
}

bool EngineFrustHost::registerNodeLibraries(const std::string& key, std::string& error)
{
    return RegisterPluginNodeLibraries(runtime.nodeLibraries(key), nodeLibraries_, error);
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

std::int64_t EngineFrustHost::requestSceneTransition(const char* sceneReference)
{
    if (activeHost == nullptr || !activeHost->sceneTransitionRequestHandler || sceneReference == nullptr || *sceneReference == '\0') return 0;
    activeHost->sceneTransitionRequestHandler(sceneReference);
    return 1;
}

const char* EngineFrustHost::activeGameId()
{
    static thread_local std::string value;
    value = (activeHost != nullptr && activeHost->activeGameIdProvider) ? activeHost->activeGameIdProvider() : std::string{};
    return value.c_str();
}

const char* EngineFrustHost::activeSceneId()
{
    static thread_local std::string value;
    value = (activeHost != nullptr && activeHost->activeSceneIdProvider) ? activeHost->activeSceneIdProvider() : std::string{};
    return value.c_str();
}

std::string EngineFrustHost::behaviorKey(const std::string& podId)
{
    return "object-behavior:" + podId;
}

std::vector<std::pair<std::int64_t, std::string>> EngineFrustHost::attachedObjectBehaviors() const
{
    std::vector<std::pair<std::int64_t, std::string>> attachments;
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const auto objects = world.Registry().view<const scene::BehaviorAttachments>();
    for (const auto entity : objects)
    {
        const auto& behaviorAttachments = objects.get<const scene::BehaviorAttachments>(entity);
        for (const auto& podId : behaviorAttachments.podIds) {
            attachments.emplace_back(static_cast<std::int64_t>(entt::to_integral(entity)), podId.toStdString());
        }
    }
    std::sort(attachments.begin(), attachments.end());
    return attachments;
}

void EngineFrustHost::ensureObjectLifecycle(std::int64_t entityId, const std::string& podId, std::int64_t tick)
{
    if (!runtime.isLoaded(behaviorKey(podId))) {
        return;
    }

    auto& lifecycle = objectLifecycles[entityId];
    if (lifecycle.spawnedPods.insert(podId).second) {
        invokeObjectHook(entityId, podId, "on_spawn", EngineFrustEvent::simulationStarted, tick);
    }
    if (playActive && lifecycle.playingPods.insert(podId).second) {
        invokeObjectHook(entityId, podId, "on_begin_play", EngineFrustEvent::simulationStarted, tick);
    }
}

void EngineFrustHost::invokeObjectHook(std::int64_t entityId, const std::string& podId, const char* hookName,
                                       EngineFrustEvent fallbackEvent, std::int64_t tick)
{
    const auto key = behaviorKey(podId);
    if (!runtime.isLoaded(key)) {
        return;
    }

    activeObjectEntityId = entityId;
    const auto hook = reinterpret_cast<ObjectLifecycleHook>(runtime.getFunction(key, hookName));
    if (hook != nullptr) {
        (void)hook(tick);
    } else {
        (void)runtime.callEvent(key, static_cast<std::int64_t>(fallbackEvent), tick);
    }
    activeObjectEntityId = -1;
}
} // namespace ce::frust
