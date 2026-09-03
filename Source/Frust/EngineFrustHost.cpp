#include "Frust/EngineFrustHost.h"
#include "Frust/EngineNodeLibraryLoader.h"

#include <algorithm>

#include "node_system/core_control_flow.h"

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
    runtime.registerHostFunction("engine_set_material_color_parameter", reinterpret_cast<void*>(&EngineFrustHost::setMaterialColorParameter));
    runtime.registerHostFunction("engine_set_material_scalar_parameter", reinterpret_cast<void*>(&EngineFrustHost::setMaterialScalarParameter));
    runtime.registerHostFunction("engine_request_scene_transition", reinterpret_cast<void*>(&EngineFrustHost::requestSceneTransition));
    runtime.registerHostFunction("engine_active_game_id", reinterpret_cast<void*>(&EngineFrustHost::activeGameId));
    runtime.registerHostFunction("engine_active_scene_id", reinterpret_cast<void*>(&EngineFrustHost::activeSceneId));
    runtime.registerHostFunction("pod_get_variable_bool", reinterpret_cast<void*>(&EngineFrustHost::podGetVariableBool));
    runtime.registerHostFunction("pod_set_variable_bool", reinterpret_cast<void*>(&EngineFrustHost::podSetVariableBool));
    runtime.registerHostFunction("pod_get_variable_int", reinterpret_cast<void*>(&EngineFrustHost::podGetVariableInt));
    runtime.registerHostFunction("pod_set_variable_int", reinterpret_cast<void*>(&EngineFrustHost::podSetVariableInt));
    runtime.registerHostFunction("pod_get_variable_string", reinterpret_cast<void*>(&EngineFrustHost::podGetVariableString));
    runtime.registerHostFunction("pod_set_variable_string", reinterpret_cast<void*>(&EngineFrustHost::podSetVariableString));
    runtime.registerHostFunction("engine_asset_exists", reinterpret_cast<void*>(&EngineFrustHost::assetExists));

    // Real, pre-existing gap fixed here: control-flow/Event/Variable node
    // types were only ever registered into PodEditorPanel's own palette
    // registry (a throwaway NodeTypeRegistry built fresh each time,
    // CopyRegistry()), never into nodeLibraries_ -- the registry
    // CompileBehaviorGraphToFrust actually receives. lowerExecChain()
    // requires libraries.FindNodeType() to succeed for every node it
    // walks, including the entry node itself, so a graph using ANY of
    // these node types could never actually compile in the running app,
    // only in NodeSystemSmoke.cpp's own separately-constructed test
    // registry. Found while wiring up Phase 4/5 of the Node/Behavior
    // Graph Foundations plan.
    {
        node_system::NodeTypeRegistry structuralTypes;
        node_system::RegisterCoreControlFlowNodes(structuralTypes);
        node_system::RegisterCoreEventNodes(structuralTypes);
        node_system::RegisterCoreVariableNodes(structuralTypes);
        node_system::RegisterCoreCapabilityNodes(structuralTypes);

        node_system::NodeLibraryDescriptor library;
        library.id = "core-structural";
        library.displayName = "Core Structural Nodes";
        library.description = "Control-flow, Event, and Variable nodes native to NodeSystem itself -- not FRust-reflected.";
        library.target = node_system::GraphTarget::Behavior;
        for (const auto& [name, descriptor] : structuralTypes.Types())
            library.nodeTypes.push_back(descriptor);

        std::string libraryError;
        nodeLibraries_.Register(std::move(library), &libraryError);
    }
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

void EngineFrustHost::setAssetExistsProvider(std::function<bool(const std::string&)> provider)
{
    assetExistsProvider = std::move(provider);
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
    // The capabilities this host actually provides, checked against every
    // loaded library's requiredCapabilities (Phase 8 of the Node/
    // Behavior Graph Foundations plan). engine.entity.query backs
    // core.entity.self (Phase 5); engine.asset.query backs the new
    // core.asset.exists node below.
    static const std::set<std::string, std::less<>> kSupportedCapabilities {
        "engine.entity.query",
        "engine.asset.query",
    };
    return RegisterPluginNodeLibraries(runtime.nodeLibraries(key), nodeLibraries_, kSupportedCapabilities, error);
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

namespace {
juce::Identifier PodVariableKey(const char* podId, const char* name) {
    return juce::Identifier(juce::String(podId != nullptr ? podId : "") + "." + juce::String(name != nullptr ? name : ""));
}
}

bool EngineFrustHost::podGetVariableBool(std::int64_t entityId, const char* podId, const char* name)
{
    if (activeHost == nullptr || entityId < 0) return false;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    const auto* state = registry.valid(entity) ? registry.try_get<scene::ObjectState>(entity) : nullptr;
    return state != nullptr && static_cast<bool>(state->values[PodVariableKey(podId, name)]);
}

std::int64_t EngineFrustHost::podSetVariableBool(std::int64_t entityId, const char* podId, const char* name, bool value)
{
    if (activeHost == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (!registry.valid(entity)) return 0;
    registry.get_or_emplace<scene::ObjectState>(entity).values.set(PodVariableKey(podId, name), value);
    return 1;
}

std::int64_t EngineFrustHost::podGetVariableInt(std::int64_t entityId, const char* podId, const char* name)
{
    if (activeHost == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    const auto* state = registry.valid(entity) ? registry.try_get<scene::ObjectState>(entity) : nullptr;
    return state != nullptr ? static_cast<std::int64_t>(state->values[PodVariableKey(podId, name)]) : 0;
}

std::int64_t EngineFrustHost::podSetVariableInt(std::int64_t entityId, const char* podId, const char* name, std::int64_t value)
{
    if (activeHost == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (!registry.valid(entity)) return 0;
    registry.get_or_emplace<scene::ObjectState>(entity).values.set(PodVariableKey(podId, name), value);
    return 1;
}

const char* EngineFrustHost::podGetVariableString(std::int64_t entityId, const char* podId, const char* name)
{
    static thread_local std::string value;
    value.clear();
    if (activeHost == nullptr || entityId < 0) return value.c_str();
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (const auto* state = registry.valid(entity) ? registry.try_get<scene::ObjectState>(entity) : nullptr) {
        value = state->values[PodVariableKey(podId, name)].toString().toStdString();
    }
    return value.c_str();
}

bool EngineFrustHost::assetExists(const char* assetDisplayName)
{
    if (activeHost == nullptr || !activeHost->assetExistsProvider || assetDisplayName == nullptr) return false;
    return activeHost->assetExistsProvider(assetDisplayName);
}

std::int64_t EngineFrustHost::podSetVariableString(std::int64_t entityId, const char* podId, const char* name, const char* value)
{
    if (activeHost == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (!registry.valid(entity)) return 0;
    registry.get_or_emplace<scene::ObjectState>(entity).values.set(PodVariableKey(podId, name), juce::String(value != nullptr ? value : ""));
    return 1;
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

std::int64_t EngineFrustHost::setMaterialColorParameter(std::int64_t entityId, const char* parameterName,
                                                          std::int64_t r255, std::int64_t g255, std::int64_t b255)
{
    if (activeHost == nullptr || entityId < 0 || parameterName == nullptr || *parameterName == '\0') return 0;

    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (!registry.valid(entity)) return 0;

    auto& overrides = registry.get_or_emplace<engine::MaterialParameterOverrides>(entity);
    overrides.colors[parameterName] = engine::Vec3{
        juce::jlimit<float>(0.0f, 255.0f, static_cast<float>(r255)) / 255.0f,
        juce::jlimit<float>(0.0f, 255.0f, static_cast<float>(g255)) / 255.0f,
        juce::jlimit<float>(0.0f, 255.0f, static_cast<float>(b255)) / 255.0f,
    };
    return 1;
}

std::int64_t EngineFrustHost::setMaterialScalarParameter(std::int64_t entityId, const char* parameterName, std::int64_t valuePerMille)
{
    if (activeHost == nullptr || entityId < 0 || parameterName == nullptr || *parameterName == '\0') return 0;

    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(activeHost->world.RegistryMutex());
    auto& registry = activeHost->world.Registry();
    if (!registry.valid(entity)) return 0;

    auto& overrides = registry.get_or_emplace<engine::MaterialParameterOverrides>(entity);
    overrides.scalars[parameterName] = static_cast<float>(valuePerMille) / 1000.0f;
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
