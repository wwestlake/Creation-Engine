#pragma once

#include <cstdint>
#include <string>

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

    bool loadBundled(std::string& error);
    void dispatch(EngineFrustEvent event, std::int64_t argument = 0);
    [[nodiscard]] bool isLoaded() const noexcept;

private:
    static std::int64_t currentTick();

    static EngineFrustHost* activeHost;

    engine::World& world;
    creation::frust::PluginRuntime runtime { "creation-engine" };
};
} // namespace ce::frust
