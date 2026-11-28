#include "Frust/EngineFrustHost.h"
#include <creation/frust/NodeLibraryLoader.h>

#include <algorithm>
#include <array>

#include "node_system/core_control_flow.h"

#include <juce_core/juce_core.h>

#include "engine/core_components.h"
#include "engine/world.h"
#include "Input/InputActionSystem.h"
#include "Physics/PhysicsComponents.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/AnimatorControl.h"
#include "Scene/Components.h"

namespace ce::frust
{
namespace
{
using ObjectLifecycleHook = std::int64_t (*)(std::int64_t);
}

// The FRust host ABI has no userdata parameter. Set the context only while
// executing JIT code. A nested invocation restores its caller's world and entity.
class EngineFrustHost::CallbackScope final
{
public:
    explicit CallbackScope(EngineFrustHost& owner, std::int64_t entityId = -1)
        : host(owner), entity(entityId), previous(current) { current = this; }
    ~CallbackScope() { current = previous; }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;

    EngineFrustHost& host;
    const std::int64_t entity;
    std::string variableValue, animationClip, gameId, sceneId;
    std::array<std::string, 4> gameplayText;
    static thread_local CallbackScope* current;

private:
    CallbackScope* const previous;
};

thread_local EngineFrustHost::CallbackScope* EngineFrustHost::CallbackScope::current = nullptr;

EngineFrustHost* EngineFrustHost::callbackHost() noexcept
{
    return CallbackScope::current != nullptr ? &CallbackScope::current->host : nullptr;
}

EngineFrustHost::EngineFrustHost(engine::World& worldToHost, bool isServerHost)
    : world(worldToHost), serverMode(isServerHost)
{
    runtime.registerHostFunction("engine_gameplay_world_id", reinterpret_cast<void*>(&EngineFrustHost::gameplayWorld));
    runtime.registerHostFunction("engine_gameplay_player_id", reinterpret_cast<void*>(&EngineFrustHost::gameplayPlayer));
    runtime.registerHostFunction("engine_gameplay_request_name", reinterpret_cast<void*>(&EngineFrustHost::gameplayRequest));
    runtime.registerHostFunction("engine_gameplay_target_id", reinterpret_cast<void*>(&EngineFrustHost::gameplayTarget));
    runtime.registerHostFunction("engine_gameplay_read_int", reinterpret_cast<void*>(&EngineFrustHost::gameplayRead));
    runtime.registerHostFunction("engine_gameplay_write_int", reinterpret_cast<void*>(&EngineFrustHost::gameplayWrite));
    runtime.registerHostFunction("engine_gameplay_publish_int", reinterpret_cast<void*>(&EngineFrustHost::gameplayPublish));
    runtime.registerHostFunction("engine_gameplay_submit", reinterpret_cast<void*>(&EngineFrustHost::gameplaySubmit));
    runtime.registerHostFunction("engine_current_tick", reinterpret_cast<void*>(&EngineFrustHost::currentTick));
    runtime.registerHostFunction("engine_runtime_is_server", reinterpret_cast<void*>(&EngineFrustHost::runtimeIsServer));
    runtime.registerHostFunction("engine_first_transform_entity", reinterpret_cast<void*>(&EngineFrustHost::firstTransformEntity));
    runtime.registerHostFunction("engine_current_object_entity", reinterpret_cast<void*>(&EngineFrustHost::currentObjectEntity));
    runtime.registerHostFunction("engine_entity_find_by_name", reinterpret_cast<void*>(&EngineFrustHost::entityFindByName));
    runtime.registerHostFunction("engine_entity_find_by_instance_id", reinterpret_cast<void*>(&EngineFrustHost::entityFindByInstanceId));
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
    runtime.registerHostFunction("engine_anim_set_active_clip", reinterpret_cast<void*>(&EngineFrustHost::animSetActiveClip));
    runtime.registerHostFunction("engine_anim_crossfade_to", reinterpret_cast<void*>(&EngineFrustHost::animCrossfadeTo));
    runtime.registerHostFunction("engine_anim_set_playback_speed_permille", reinterpret_cast<void*>(&EngineFrustHost::animSetPlaybackSpeedPerMille));
    runtime.registerHostFunction("engine_anim_get_active_clip_name", reinterpret_cast<void*>(&EngineFrustHost::animGetActiveClipName));
    runtime.registerHostFunction("engine_anim_get_clip_duration_millis", reinterpret_cast<void*>(&EngineFrustHost::animGetClipDurationMillis));
    runtime.registerHostFunction("engine_anim_is_blending", reinterpret_cast<void*>(&EngineFrustHost::animIsBlending));
    runtime.registerHostFunction("engine_input_is_action_active", reinterpret_cast<void*>(&EngineFrustHost::inputIsActionActive));
    runtime.registerHostFunction("engine_input_was_action_pressed", reinterpret_cast<void*>(&EngineFrustHost::inputWasActionPressed));
    runtime.registerHostFunction("engine_input_was_action_released", reinterpret_cast<void*>(&EngineFrustHost::inputWasActionReleased));
    runtime.registerHostFunction("engine_input_get_action_value_permille", reinterpret_cast<void*>(&EngineFrustHost::inputGetActionValuePerMille));
    runtime.registerHostFunction("engine_physics_set_rigid_body", reinterpret_cast<void*>(&EngineFrustHost::physicsSetRigidBody));
    runtime.registerHostFunction("engine_physics_set_collider_shape", reinterpret_cast<void*>(&EngineFrustHost::physicsSetColliderShape));
    runtime.registerHostFunction("engine_physics_apply_force", reinterpret_cast<void*>(&EngineFrustHost::physicsApplyForce));
    runtime.registerHostFunction("engine_physics_apply_impulse", reinterpret_cast<void*>(&EngineFrustHost::physicsApplyImpulse));
    runtime.registerHostFunction("engine_physics_set_linear_velocity", reinterpret_cast<void*>(&EngineFrustHost::physicsSetLinearVelocity));
    runtime.registerHostFunction("engine_physics_get_linear_velocity_x", reinterpret_cast<void*>(&EngineFrustHost::physicsGetLinearVelocityX));
    runtime.registerHostFunction("engine_physics_get_linear_velocity_y", reinterpret_cast<void*>(&EngineFrustHost::physicsGetLinearVelocityY));
    runtime.registerHostFunction("engine_physics_get_linear_velocity_z", reinterpret_cast<void*>(&EngineFrustHost::physicsGetLinearVelocityZ));
    runtime.registerHostFunction("engine_physics_raycast", reinterpret_cast<void*>(&EngineFrustHost::physicsRaycast));
    runtime.registerHostFunction("engine_physics_raycast_hit_entity", reinterpret_cast<void*>(&EngineFrustHost::physicsRaycastHitEntity));
    runtime.registerHostFunction("engine_physics_raycast_hit_distance", reinterpret_cast<void*>(&EngineFrustHost::physicsRaycastHitDistance));
    runtime.registerHostFunction("engine_physics_raycast_normal_x", reinterpret_cast<void*>(&EngineFrustHost::physicsRaycastNormalX));
    runtime.registerHostFunction("engine_physics_raycast_normal_y", reinterpret_cast<void*>(&EngineFrustHost::physicsRaycastNormalY));
    runtime.registerHostFunction("engine_physics_raycast_normal_z", reinterpret_cast<void*>(&EngineFrustHost::physicsRaycastNormalZ));

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
        node_system::RegisterCoreAnimationNodes(structuralTypes);
        node_system::RegisterCoreInputNodes(structuralTypes);
        node_system::RegisterCorePhysicsNodes(structuralTypes);

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
    const CallbackScope scope(*this);
    runtime.unloadAll();
}

bool EngineFrustHost::load(const std::string& pluginPath, std::string& error)
{
    const CallbackScope scope(*this);
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
    const CallbackScope scope(*this);
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
    const CallbackScope scope(*this);
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

bool EngineFrustHost::loadObjectBehaviorSource(const std::string& podId, const std::string& source,
                                                const ReadModule& readModule, std::string& error)
{
    const CallbackScope scope(*this);
    ::frust::CompileRequest request;
    request.sources.push_back({ podId + ".frust", source });
    request.siblingFiles = [&readModule](const std::string& name, std::string& contents) {
        const auto module = juce::String(name).replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
        if (!module.endsWith(".frust") || module.containsAnyOf(":") || module.startsWith("."))
            return false;
        // The built-in library is the same module used by the graph editor.
        if (module != "CoreNodesLibrary.frust")
            return readModule && readModule(module.toStdString(), contents);
        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto file = executable.getSiblingFile("plugins").getChildFile(module);
        if (!file.existsAsFile())
            return false;
        contents = file.loadFileAsString().toStdString();
        return true;
    };
    return runtime.loadSource(behaviorKey(podId), request, error);
}

std::int64_t EngineFrustHost::dispatch(EngineFrustEvent event, std::int64_t argument)
{
    const CallbackScope scope(*this);
    return runtime.callEvent(static_cast<std::int64_t>(event), argument);
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
    prePhysicsTick(tick);
    postPhysicsTick(tick);
}

void EngineFrustHost::prePhysicsTick(std::int64_t tick)
{
    if (!playActive) {
        return;
    }

    dispatch(EngineFrustEvent::simulationTick, tick);
    // Input Combo Events plan: fetched once per tick() call, not once per
    // (entity, pod) pair below -- InputActionSystem::PollOncePerFrame()
    // already ran this same tick (MainComponent::timerCallback, before
    // Simulation::Step/FoundationGameplay::Step/this call), so this is
    // exactly "collected and held until start of the frame, available to
    // everything that runs before OpenGL."
    const auto firedCombos = inputActionSystem_ != nullptr ? inputActionSystem_->FiredCombos() : juce::StringArray{};
    for (const auto& [entityId, podId] : attachedObjectBehaviors())
    {
        ensureObjectLifecycle(entityId, podId, tick);
        if (!objectLifecycles[entityId].playingPods.contains(podId)) {
            continue;
        }
        // Animation Control plan Phase 3: BehaviorPaused freezes just this
        // entity's on_tick -- lifecycle hooks above (on_spawn/on_begin_play)
        // still ran normally, so a later un-pause doesn't need to re-fire
        // one-time setup. The rest of the world (and this entity's own
        // already-commanded animation, sampled independently in
        // ViewportComponent) keeps running.
        if (isBehaviorPaused(entityId)) {
            continue;
        }
        invokeObjectHook(entityId, podId, "on_tick", EngineFrustEvent::simulationTick, tick);
        for (const auto& comboName : firedCombos) {
            // Same derivation PodEditorPanel.cpp's compile pass uses (via
            // this identical function) -- a hand-rolled "on_action_" +
            // name here would silently diverge from the compiled hook
            // name the moment a combo's display name needs sanitizing
            // (spaces, punctuation) for FRust identifier rules.
            const auto hookName = node_system::EventNodeFrustFunctionName("core.input.combo." + comboName.toStdString());
            if (!hookName.empty()) {
                invokeObjectHook(entityId, podId, hookName.c_str(), EngineFrustEvent::simulationTick, tick);
            }
        }
    }
}

void EngineFrustHost::postPhysicsTick(std::int64_t tick)
{
    if (!playActive || physicsWorld_ == nullptr)
        return;

    const auto collisionEvents = physicsWorld_->DrainCollisionEvents();
    for (const auto& [entityId, podId] : attachedObjectBehaviors())
    {
        if (!objectLifecycles[entityId].playingPods.contains(podId) || isBehaviorPaused(entityId))
            continue;
        for (const auto& collisionEvent : collisionEvents) {
            if (collisionEvent.entityA != entityId && collisionEvent.entityB != entityId) {
                continue;
            }
            const char* typeName = collisionEvent.kind == physics::CollisionEvent::Kind::Begin ? "core.physics.onCollisionEnter"
                                  : collisionEvent.kind == physics::CollisionEvent::Kind::Persist ? "core.physics.onCollisionStay"
                                                                                                   : "core.physics.onCollisionExit";
            const auto hookName = node_system::EventNodeFrustFunctionName(typeName);
            if (!hookName.empty()) {
                invokeObjectHook(entityId, podId, hookName.c_str(), EngineFrustEvent::simulationTick, tick);
            }
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

void EngineFrustHost::releaseLevel(std::int64_t tick)
{
    endPlay(tick);
    for (const auto& [entityId, podId] : attachedObjectBehaviors()) {
        (void)podId;
        notifyObjectDestroyed(static_cast<entt::entity>(entityId), tick);
    }
    objectLifecycles.clear();
    lastRaycastHit_ = {};
}

void EngineFrustHost::unloadObjectBehaviors()
{
    const CallbackScope scope(*this);
    for (const auto& key : runtime.loadedPluginKeys()) {
        if (key.starts_with("object-behavior:"))
            runtime.unload(key);
    }
}

std::vector<std::string> EngineFrustHost::behaviorIds() const
{
    std::vector<std::string> result;
    for (const auto& [entityId, podId] : attachedObjectBehaviors()) {
        (void)entityId;
        result.push_back(podId);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
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

bool EngineFrustHost::RefreshComboEventNodes(const std::vector<std::string>& comboNames, std::string& error)
{
    return nodeLibraries_.ReplaceLibrary(node_system::BuildInputComboEventLibrary(comboNames), &error);
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
    return creation::frust::RegisterPluginNodeLibraries(runtime.nodeLibraries(key), nodeLibraries_, kSupportedCapabilities, error);
}

const char* EngineFrustHost::gameplayText(std::size_t field)
{
    auto* host = callbackHost();
    if (host == nullptr) return "";
    const std::array sources{&host->gameplay_.world, &host->gameplay_.player, &host->gameplay_.request, &host->gameplay_.target};
    auto& value = CallbackScope::current->gameplayText[field];
    value = *sources[field] ? (*sources[field])() : std::string{};
    return value.c_str();
}
const char* EngineFrustHost::gameplayWorld() { return gameplayText(0); }
const char* EngineFrustHost::gameplayPlayer() { return gameplayText(1); }
const char* EngineFrustHost::gameplayRequest() { return gameplayText(2); }
const char* EngineFrustHost::gameplayTarget() { return gameplayText(3); }

std::int64_t EngineFrustHost::gameplayRead(const char* key)
{
    const auto* host = callbackHost();
    return host != nullptr && key != nullptr && host->gameplay_.read ? host->gameplay_.read(key) : 0;
}
bool EngineFrustHost::gameplayWrite(const char* key, std::int64_t value)
{
    const auto* host = callbackHost();
    return host != nullptr && key != nullptr && host->gameplay_.write && host->gameplay_.write(key, value, false);
}
bool EngineFrustHost::gameplayPublish(const char* key, std::int64_t value)
{
    const auto* host = callbackHost();
    return host != nullptr && key != nullptr && host->gameplay_.write && host->gameplay_.write(key, value, true);
}
bool EngineFrustHost::gameplaySubmit(const char* name, const char* target)
{
    const auto* host = callbackHost();
    return host != nullptr && name != nullptr && target != nullptr && host->gameplay_.submit && host->gameplay_.submit(name, target);
}

void EngineFrustHost::gameplayEvent(std::int64_t recipient, const char* name, std::int64_t tick)
{
    for (const auto& [entityId, pod] : attachedObjectBehaviors()) {
        if (recipient != -1 && recipient != entityId) continue;
        const auto hook = reinterpret_cast<ObjectLifecycleHook>(runtime.getFunction(behaviorKey(pod), name));
        if (hook == nullptr || isBehaviorPaused(entityId)) continue;
        const CallbackScope scope(*this, entityId);
        (void)hook(tick);
    }
}

std::int64_t EngineFrustHost::currentTick()
{
    auto* const host = callbackHost();
    return host != nullptr ? static_cast<std::int64_t>(host->world.CurrentTick()) : 0;
}

std::int64_t EngineFrustHost::runtimeIsServer()
{
    auto* const host = callbackHost();
    return host != nullptr && host->isServer() ? 1 : 0;
}

std::int64_t EngineFrustHost::firstTransformEntity()
{
    auto* const host = callbackHost();
    if (host == nullptr) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    const auto view = host->world.Registry().view<engine::Transform>();
    const auto first = view.begin();
    if (first == view.end()) {
        return -1;
    }
    return static_cast<std::int64_t>(entt::to_integral(*first));
}

std::int64_t EngineFrustHost::currentObjectEntity()
{
    return CallbackScope::current != nullptr ? CallbackScope::current->entity : -1;
}

std::int64_t EngineFrustHost::entityFindByName(const char* name)
{
    auto* const host = callbackHost();
    if (host == nullptr || name == nullptr) return -1;
    const juce::String target(name);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    const auto view = host->world.Registry().view<scene::Name>();
    for (const auto entity : view)
        if (view.get<scene::Name>(entity).value == target)
            return static_cast<std::int64_t>(entt::to_integral(entity));
    return -1;
}

std::int64_t EngineFrustHost::entityFindByInstanceId(const char* instanceId)
{
    auto* const host = callbackHost();
    if (host == nullptr || instanceId == nullptr) return -1;
    const juce::String target(instanceId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    const auto view = host->world.Registry().view<scene::InstanceId>();
    for (const auto entity : view)
        if (view.get<scene::InstanceId>(entity).value == target)
            return static_cast<std::int64_t>(entt::to_integral(entity));
    return -1;
}

namespace {
juce::Identifier PodVariableKey(const char* podId, const char* name) {
    return juce::Identifier(juce::String(podId != nullptr ? podId : "") + "." + juce::String(name != nullptr ? name : ""));
}
}

bool EngineFrustHost::podGetVariableBool(std::int64_t entityId, const char* podId, const char* name)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return false;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    const auto* state = registry.valid(entity) ? registry.try_get<scene::ObjectState>(entity) : nullptr;
    return state != nullptr && static_cast<bool>(state->values[PodVariableKey(podId, name)]);
}

std::int64_t EngineFrustHost::podSetVariableBool(std::int64_t entityId, const char* podId, const char* name, bool value)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity)) return 0;
    registry.get_or_emplace<scene::ObjectState>(entity).values.set(PodVariableKey(podId, name), value);
    return 1;
}

std::int64_t EngineFrustHost::podGetVariableInt(std::int64_t entityId, const char* podId, const char* name)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    const auto* state = registry.valid(entity) ? registry.try_get<scene::ObjectState>(entity) : nullptr;
    return state != nullptr ? static_cast<std::int64_t>(state->values[PodVariableKey(podId, name)]) : 0;
}

std::int64_t EngineFrustHost::podSetVariableInt(std::int64_t entityId, const char* podId, const char* name, std::int64_t value)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity)) return 0;
    registry.get_or_emplace<scene::ObjectState>(entity).values.set(PodVariableKey(podId, name), value);
    return 1;
}

const char* EngineFrustHost::podGetVariableString(std::int64_t entityId, const char* podId, const char* name)
{
    auto* const host = callbackHost();
    if (CallbackScope::current == nullptr) return "";
    auto& value = CallbackScope::current->variableValue;
    value.clear();
    if (host == nullptr || entityId < 0) return value.c_str();
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (const auto* state = registry.valid(entity) ? registry.try_get<scene::ObjectState>(entity) : nullptr) {
        value = state->values[PodVariableKey(podId, name)].toString().toStdString();
    }
    return value.c_str();
}

bool EngineFrustHost::assetExists(const char* assetDisplayName)
{
    auto* const host = callbackHost();
    if (host == nullptr || !host->assetExistsProvider || assetDisplayName == nullptr) return false;
    return host->assetExistsProvider(assetDisplayName);
}

std::int64_t EngineFrustHost::animSetActiveClip(std::int64_t entityId, const char* clipName)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0 || clipName == nullptr || *clipName == '\0') return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    auto* animator = registry.valid(entity) ? registry.try_get<scene::Animator>(entity) : nullptr;
    if (animator == nullptr || animator->clips == nullptr) return 0;

    const auto& clips = *animator->clips;
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == clipName) {
            animator->activeClip = static_cast<int>(i);
            animator->time = 0.0f;
            animator->blendFromClip = -1;
            animator->blendFromTime = 0.0f;
            animator->blendTime = 0.0f;
            animator->blendDuration = 0.0f;
            return 1;
        }
    }
    return 0;
}

std::int64_t EngineFrustHost::animCrossfadeTo(std::int64_t entityId, const char* clipName, std::int64_t blendMillis)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    auto* animator = registry.valid(entity) ? registry.try_get<scene::Animator>(entity) : nullptr;
    if (animator == nullptr) return 0;
    return scene::CrossfadeAnimatorTo(*animator, clipName, static_cast<int>(blendMillis)) ? 1 : 0;
}

std::int64_t EngineFrustHost::animSetPlaybackSpeedPerMille(std::int64_t entityId, std::int64_t speedPerMille)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    auto* animator = registry.valid(entity) ? registry.try_get<scene::Animator>(entity) : nullptr;
    if (animator == nullptr) return 0;
    animator->playbackSpeed = static_cast<float>(speedPerMille) / 1000.0f;
    return 1;
}

const char* EngineFrustHost::animGetActiveClipName(std::int64_t entityId)
{
    auto* const host = callbackHost();
    if (CallbackScope::current == nullptr) return "";
    auto& value = CallbackScope::current->animationClip;
    value.clear();
    if (host == nullptr || entityId < 0) return value.c_str();
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    const auto* animator = registry.valid(entity) ? registry.try_get<const scene::Animator>(entity) : nullptr;
    if (animator != nullptr && animator->clips != nullptr && animator->activeClip >= 0 &&
        static_cast<std::size_t>(animator->activeClip) < animator->clips->size()) {
        value = (*animator->clips)[static_cast<std::size_t>(animator->activeClip)].name.toStdString();
    }
    return value.c_str();
}

std::int64_t EngineFrustHost::animGetClipDurationMillis(std::int64_t entityId, const char* clipName)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0 || clipName == nullptr) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    const auto* animator = registry.valid(entity) ? registry.try_get<const scene::Animator>(entity) : nullptr;
    if (animator == nullptr || animator->clips == nullptr) return 0;
    for (const auto& clip : *animator->clips) {
        if (clip.name == clipName) {
            return static_cast<std::int64_t>(clip.duration * 1000.0f);
        }
    }
    return 0;
}

bool EngineFrustHost::animIsBlending(std::int64_t entityId)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return false;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    const auto* animator = registry.valid(entity) ? registry.try_get<const scene::Animator>(entity) : nullptr;
    return animator != nullptr && animator->blendFromClip >= 0;
}

bool EngineFrustHost::inputIsActionActive(const char* actionName)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->inputActionSystem_ == nullptr || actionName == nullptr) return false;
    return host->inputActionSystem_->IsActionActive(actionName);
}

bool EngineFrustHost::inputWasActionPressed(const char* actionName)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->inputActionSystem_ == nullptr || actionName == nullptr) return false;
    return host->inputActionSystem_->WasActionPressed(actionName);
}

bool EngineFrustHost::inputWasActionReleased(const char* actionName)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->inputActionSystem_ == nullptr || actionName == nullptr) return false;
    return host->inputActionSystem_->WasActionReleased(actionName);
}

std::int64_t EngineFrustHost::inputGetActionValuePerMille(const char* actionName)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->inputActionSystem_ == nullptr || actionName == nullptr) return 0;
    return host->inputActionSystem_->GetActionValuePerMille(actionName);
}

std::int64_t EngineFrustHost::physicsSetRigidBody(std::int64_t entityId, std::int64_t motionType, double mass,
                                                  double friction, double restitution,
                                                  double linearDamping, double angularDamping)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity)) return 0;

    auto& rigidBody = registry.get_or_emplace<physics::RigidBodyComponent>(entity);
    rigidBody.motionType = motionType == 0 ? physics::MotionType::Static
                          : motionType == 1 ? physics::MotionType::Kinematic
                                            : physics::MotionType::Dynamic;
    rigidBody.mass = static_cast<float>(mass);
    rigidBody.friction = static_cast<float>(friction);
    rigidBody.restitution = static_cast<float>(restitution);
    rigidBody.linearDamping = static_cast<float>(linearDamping);
    rigidBody.angularDamping = static_cast<float>(angularDamping);
    // Deliberately does NOT create the Jolt body itself -- PhysicsWorld's
    // own reconciliation pass in Advance() does that once both this and
    // ColliderComponent are present, so a Pod can set the two config
    // nodes in either order (Jolt vendoring plan, Decision 3).
    return 1;
}

std::int64_t EngineFrustHost::physicsSetColliderShape(std::int64_t entityId, std::int64_t shapeKind,
                                                      double halfExtentX, double halfExtentY, double halfExtentZ,
                                                      double radius, double halfHeight, std::int64_t collisionLayer,
                                                      bool isSensor)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity)) return 0;

    auto& collider = registry.get_or_emplace<physics::ColliderComponent>(entity);
    collider.shape = shapeKind == 1 ? physics::ColliderShapeKind::Sphere
                    : shapeKind == 2 ? physics::ColliderShapeKind::Capsule
                                     : physics::ColliderShapeKind::Box;
    collider.halfExtentX = static_cast<float>(halfExtentX);
    collider.halfExtentY = static_cast<float>(halfExtentY);
    collider.halfExtentZ = static_cast<float>(halfExtentZ);
    collider.radius = static_cast<float>(radius);
    collider.halfHeight = static_cast<float>(halfHeight);
    collider.collisionLayer = static_cast<std::uint16_t>(collisionLayer);
    collider.isSensor = isSensor;
    return 1;
}

std::int64_t EngineFrustHost::physicsApplyForce(std::int64_t entityId, double x, double y, double z)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr || entityId < 0) return 0;
    host->physicsWorld_->ApplyForce(host->world, entityId,
                                          static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return 1;
}

std::int64_t EngineFrustHost::physicsApplyImpulse(std::int64_t entityId, double x, double y, double z)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr || entityId < 0) return 0;
    host->physicsWorld_->ApplyImpulse(host->world, entityId,
                                            static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return 1;
}

std::int64_t EngineFrustHost::physicsSetLinearVelocity(std::int64_t entityId, double x, double y, double z)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr || entityId < 0) return 0;
    host->physicsWorld_->SetLinearVelocity(host->world, entityId,
                                                 static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return 1;
}

double EngineFrustHost::physicsGetLinearVelocityX(std::int64_t entityId)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr || entityId < 0) return 0.0;
    float x = 0, y = 0, z = 0;
    host->physicsWorld_->GetLinearVelocity(host->world, entityId, x, y, z);
    return static_cast<double>(x);
}

double EngineFrustHost::physicsGetLinearVelocityY(std::int64_t entityId)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr || entityId < 0) return 0.0;
    float x = 0, y = 0, z = 0;
    host->physicsWorld_->GetLinearVelocity(host->world, entityId, x, y, z);
    return static_cast<double>(y);
}

double EngineFrustHost::physicsGetLinearVelocityZ(std::int64_t entityId)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr || entityId < 0) return 0.0;
    float x = 0, y = 0, z = 0;
    host->physicsWorld_->GetLinearVelocity(host->world, entityId, x, y, z);
    return static_cast<double>(z);
}

bool EngineFrustHost::physicsRaycast(double originX, double originY, double originZ,
                                     double dirX, double dirY, double dirZ, double maxDistance)
{
    auto* const host = callbackHost();
    if (host == nullptr || host->physicsWorld_ == nullptr) return false;
    const auto hit = host->physicsWorld_->CastRay(
        static_cast<float>(originX), static_cast<float>(originY), static_cast<float>(originZ),
        static_cast<float>(dirX), static_cast<float>(dirY), static_cast<float>(dirZ),
        static_cast<float>(maxDistance));
    host->lastRaycastHit_.hit = hit.hit;
    host->lastRaycastHit_.hitEntity = hit.hitEntity;
    host->lastRaycastHit_.distance = static_cast<double>(hit.distance);
    host->lastRaycastHit_.normalX = static_cast<double>(hit.normalX);
    host->lastRaycastHit_.normalY = static_cast<double>(hit.normalY);
    host->lastRaycastHit_.normalZ = static_cast<double>(hit.normalZ);
    return hit.hit;
}

std::int64_t EngineFrustHost::physicsRaycastHitEntity()
{
    auto* const host = callbackHost();
    return host != nullptr ? host->lastRaycastHit_.hitEntity : -1;
}

double EngineFrustHost::physicsRaycastHitDistance()
{
    auto* const host = callbackHost();
    return host != nullptr ? host->lastRaycastHit_.distance : 0.0;
}

double EngineFrustHost::physicsRaycastNormalX()
{
    auto* const host = callbackHost();
    return host != nullptr ? host->lastRaycastHit_.normalX : 0.0;
}

double EngineFrustHost::physicsRaycastNormalY()
{
    auto* const host = callbackHost();
    return host != nullptr ? host->lastRaycastHit_.normalY : 0.0;
}

double EngineFrustHost::physicsRaycastNormalZ()
{
    auto* const host = callbackHost();
    return host != nullptr ? host->lastRaycastHit_.normalZ : 0.0;
}

std::int64_t EngineFrustHost::podSetVariableString(std::int64_t entityId, const char* podId, const char* name, const char* value)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) return 0;
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity)) return 0;
    registry.get_or_emplace<scene::ObjectState>(entity).values.set(PodVariableKey(podId, name), juce::String(value != nullptr ? value : ""));
    return 1;
}

std::int64_t EngineFrustHost::setPositionX(std::int64_t entityId, std::int64_t positionX)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0) {
        return 0;
    }

    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity) || !registry.all_of<engine::Transform>(entity)) {
        return 0;
    }

    registry.get<engine::Transform>(entity).position.x = static_cast<float>(positionX);
    return 1;
}

std::int64_t EngineFrustHost::setMaterialColorParameter(std::int64_t entityId, const char* parameterName,
                                                          std::int64_t r255, std::int64_t g255, std::int64_t b255)
{
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0 || parameterName == nullptr || *parameterName == '\0') return 0;

    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
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
    auto* const host = callbackHost();
    if (host == nullptr || entityId < 0 || parameterName == nullptr || *parameterName == '\0') return 0;

    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(host->world.RegistryMutex());
    auto& registry = host->world.Registry();
    if (!registry.valid(entity)) return 0;

    auto& overrides = registry.get_or_emplace<engine::MaterialParameterOverrides>(entity);
    overrides.scalars[parameterName] = static_cast<float>(valuePerMille) / 1000.0f;
    return 1;
}

std::int64_t EngineFrustHost::requestSceneTransition(const char* sceneReference)
{
    auto* const host = callbackHost();
    if (host == nullptr || !host->sceneTransitionRequestHandler || sceneReference == nullptr || *sceneReference == '\0') return 0;
    host->sceneTransitionRequestHandler(sceneReference);
    return 1;
}

const char* EngineFrustHost::activeGameId()
{
    auto* const host = callbackHost();
    if (CallbackScope::current == nullptr) return "";
    auto& value = CallbackScope::current->gameId;
    value = (host != nullptr && host->activeGameIdProvider) ? host->activeGameIdProvider() : std::string{};
    return value.c_str();
}

const char* EngineFrustHost::activeSceneId()
{
    auto* const host = callbackHost();
    if (CallbackScope::current == nullptr) return "";
    auto& value = CallbackScope::current->sceneId;
    value = (host != nullptr && host->activeSceneIdProvider) ? host->activeSceneIdProvider() : std::string{};
    return value.c_str();
}

std::string EngineFrustHost::behaviorKey(const std::string& podId)
{
    return "object-behavior:" + podId;
}

bool EngineFrustHost::isBehaviorPaused(std::int64_t entityId) const
{
    if (entityId < 0) {
        return false;
    }
    const auto entity = static_cast<entt::entity>(entityId);
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    const auto& registry = world.Registry();
    return registry.valid(entity) && registry.all_of<scene::BehaviorPaused>(entity);
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

    const CallbackScope scope(*this, entityId);
    const auto hook = reinterpret_cast<ObjectLifecycleHook>(runtime.getFunction(key, hookName));
    if (hook != nullptr) {
        (void)hook(tick);
    } else {
        (void)runtime.callEvent(key, static_cast<std::int64_t>(fallbackEvent), tick);
    }
}
} // namespace ce::frust
