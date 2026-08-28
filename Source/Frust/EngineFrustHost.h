#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <creation/frust/PluginRuntime.h>

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
    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] bool isObjectBehaviorLoaded(const std::string& podId) const noexcept;

private:
    static std::int64_t currentTick();
    static std::int64_t firstTransformEntity();
    static std::int64_t currentObjectEntity();
    static std::int64_t setPositionX(std::int64_t entityId, std::int64_t positionX);
    [[nodiscard]] static std::string behaviorKey(const std::string& podId);
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::string>> attachedObjectBehaviors() const;
    void ensureObjectLifecycle(std::int64_t entityId, const std::string& podId, std::int64_t tick);
    void invokeObjectHook(std::int64_t entityId, const std::string& podId, const char* hookName,
                          EngineFrustEvent fallbackEvent, std::int64_t tick);

    struct ObjectLifecycle
    {
        std::set<std::string> spawnedPods;
        std::set<std::string> playingPods;
    };

    static EngineFrustHost* activeHost;

    engine::World& world;
    creation::frust::PluginRuntime runtime { "creation-engine" };
    std::int64_t activeObjectEntityId = -1;
    bool playActive = false;
    std::unordered_map<std::int64_t, ObjectLifecycle> objectLifecycles;
};
} // namespace ce::frust
