#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <creation/frust/PluginRuntime.h>
#include "node_system/node_library.h"

namespace ce::engine
{
class World;
}

namespace ce::frust
{
enum class EngineFrustEvent : std::int64_t
{
    simulationStarted = 1,
    simulationPaused = 2,
    simulationTick = 3
};

// Owns the Engine's first FRust capability boundary. New capabilities are
// registered deliberately here rather than exposing engine internals to every
// plugin by default.
class EngineFrustHost final
{
public:
    explicit EngineFrustHost(engine::World& world);
    ~EngineFrustHost();

    EngineFrustHost(const EngineFrustHost&) = delete;
    EngineFrustHost& operator=(const EngineFrustHost&) = delete;

    bool load(const std::string& pluginPath, std::string& error);
    bool loadBundled(std::string& error);
    bool loadObjectBehavior(const std::string& podId, const std::string& pluginPath, std::string& error);
    void dispatch(EngineFrustEvent event, std::int64_t argument = 0);
    void prepareLevel(std::int64_t tick);
    void beginPlay(std::int64_t tick);
    void tick(std::int64_t tick);
    void endPlay(std::int64_t tick);
    void notifyObjectDestroyed(entt::entity entity, std::int64_t tick);

    // Game and scene references cross the FRust boundary as UTF-8 names/IDs,
    // never as encoded numeric tokens. The owner of the running game resolves
    // them against its current scene catalog.
    void setSceneTransitionRequestHandler(std::function<void(const std::string&)> handler);
    void setActiveGameIdProvider(std::function<std::string()> provider);
    void setActiveSceneIdProvider(std::function<std::string()> provider);
    // Backs core.asset.exists (Phase 8 of the Node/Behavior Graph
    // Foundations plan) -- the host doesn't own ProjectSession/
    // AssetCatalog directly (MainComponent does), same provider-callback
    // pattern as the two providers just above, not a new convention.
    void setAssetExistsProvider(std::function<bool(const std::string&)> provider);
    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] bool isObjectBehaviorLoaded(const std::string& podId) const noexcept;
    [[nodiscard]] const node_system::NodeLibraryRegistry& nodeLibraries() const noexcept { return nodeLibraries_; }

private:
    static std::int64_t currentTick();
    static std::int64_t firstTransformEntity();
    static std::int64_t currentObjectEntity();
    static std::int64_t setPositionX(std::int64_t entityId, std::int64_t positionX);
    // r/g/b are 0-255 -- the FFI boundary here only carries i64 (see every
    // other extern fn in EngineLifecycle.frust; this doesn't introduce a
    // new convention), so an ordinary byte-color encoding is the natural
    // fit for a value FRust scripts will type as literal integers anyway.
    static std::int64_t setMaterialColorParameter(std::int64_t entityId, const char* parameterName,
                                                   std::int64_t r255, std::int64_t g255, std::int64_t b255);
    // value is per-mille (0-1000 representing 0.0-1.0) -- more headroom
    // than a byte for scalar parameters like roughness/an animation phase
    // that aren't naturally 0-255 color channels.
    static std::int64_t setMaterialScalarParameter(std::int64_t entityId, const char* parameterName, std::int64_t valuePerMille);
    static std::int64_t requestSceneTransition(const char* sceneReference);
    static const char* activeGameId();
    static const char* activeSceneId();
    // Pod Variable get/set (Node/Behavior Graph Foundations plan Phase 5)
    // -- storage is the entity's existing ObjectState.values
    // (juce::NamedValueSet, already generic, already serialized), keyed
    // by "<podId>.<name>" so two attached Pods can't collide on a
    // variable name. Bool crosses the FFI boundary directly -- verified
    // clean in both directions by FrustLang's own dedicated test harness
    // (LANGUAGE_GAPS.md #9), not just assumed. Float is deliberately not
    // included yet: no equivalent dedicated f64-FFI verification exists
    // in this codebase to point to, unlike bool/i64/String which all do.
    static bool podGetVariableBool(std::int64_t entityId, const char* podId, const char* name);
    static std::int64_t podSetVariableBool(std::int64_t entityId, const char* podId, const char* name, bool value);
    static std::int64_t podGetVariableInt(std::int64_t entityId, const char* podId, const char* name);
    static std::int64_t podSetVariableInt(std::int64_t entityId, const char* podId, const char* name, std::int64_t value);
    static const char* podGetVariableString(std::int64_t entityId, const char* podId, const char* name);
    static std::int64_t podSetVariableString(std::int64_t entityId, const char* podId, const char* name, const char* value);
    static bool assetExists(const char* assetDisplayName);
    // Animation Control plan Phase 2 -- host-extern nodes backing
    // Domain::Animation (RegisterCoreAnimationNodes), reading/writing the
    // named entity's scene::Animator. Same i64/string/bool-only FFI
    // convention as every function above (no float parameter anywhere in
    // this class) -- durations/speeds cross as milliseconds/per-mille
    // integers, decoded to float only on this side of the boundary.
    static std::int64_t animSetActiveClip(std::int64_t entityId, const char* clipName);
    static std::int64_t animCrossfadeTo(std::int64_t entityId, const char* clipName, std::int64_t blendMillis);
    static std::int64_t animSetPlaybackSpeedPerMille(std::int64_t entityId, std::int64_t speedPerMille);
    static const char* animGetActiveClipName(std::int64_t entityId);
    static std::int64_t animGetClipDurationMillis(std::int64_t entityId, const char* clipName);
    static bool animIsBlending(std::int64_t entityId);
    [[nodiscard]] static std::string behaviorKey(const std::string& podId);
    [[nodiscard]] bool isBehaviorPaused(std::int64_t entityId) const;
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::string>> attachedObjectBehaviors() const;
    void ensureObjectLifecycle(std::int64_t entityId, const std::string& podId, std::int64_t tick);
    void invokeObjectHook(std::int64_t entityId, const std::string& podId, const char* hookName,
                          EngineFrustEvent fallbackEvent, std::int64_t tick);
    bool registerNodeLibraries(const std::string& key, std::string& error);

    struct ObjectLifecycle
    {
        std::set<std::string> spawnedPods;
        std::set<std::string> playingPods;
    };

    static EngineFrustHost* activeHost;

    engine::World& world;
    creation::frust::PluginRuntime runtime { "creation-engine" };
    node_system::NodeLibraryRegistry nodeLibraries_;
    std::int64_t activeObjectEntityId = -1;
    bool playActive = false;
    std::function<void(const std::string&)> sceneTransitionRequestHandler;
    std::function<std::string()> activeGameIdProvider;
    std::function<std::string()> activeSceneIdProvider;
    std::function<bool(const std::string&)> assetExistsProvider;
    std::unordered_map<std::int64_t, ObjectLifecycle> objectLifecycles;
};
} // namespace ce::frust
