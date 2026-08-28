#include "Frust/EngineFrustHost.h"

#include <JuceHeader.h>

#include "engine/world.h"

namespace ce::frust
{
EngineFrustHost* EngineFrustHost::activeHost = nullptr;

EngineFrustHost::EngineFrustHost(engine::World& worldToHost)
    : world(worldToHost)
{
    activeHost = this;
    runtime.registerHostFunction("engine_current_tick", reinterpret_cast<void*>(&EngineFrustHost::currentTick));
}

EngineFrustHost::~EngineFrustHost()
{
    runtime.unload();
    if (activeHost == this) {
        activeHost = nullptr;
    }
}

bool EngineFrustHost::loadBundled(std::string& error)
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto pluginFile = executable.getParentDirectory().getChildFile("plugins").getChildFile("EngineLifecycle.frust");
    if (!pluginFile.existsAsFile()) {
        error = "Bundled EngineLifecycle.frust plugin was not found beside the executable.";
        return false;
    }

    return runtime.load(pluginFile.getFullPathName().toStdString(), error);
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
} // namespace ce::frust
