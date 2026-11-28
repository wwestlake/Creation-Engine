#include "RuntimeDeveloperInspection.h"

#include "Frust/PodCatalog.h"
#include "Scene/Components.h"

namespace ce::runtime
{

RuntimeGameSource DeveloperFixture()
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("DJEHUTI_DEV_RUNTIME_FIXTURE", {});
    if (directory.isEmpty())
        return {};

    const juce::File root(directory);
    RuntimeGameSource source;
    source.game.id = "runtime-fixture";
    source.game.name = "Runtime parity fixture";
    source.game.entrySceneId = "entry";
    source.game.scenes.add(project::SceneDocumentInfo{ "entry", "Entry", {} });
    source.game.scenes.add(project::SceneDocumentInfo{ "next", "Next Scene", {} });
    source.readScene = [root](const auto& scene, auto& state, auto& error) {
        const auto file = root.getChildFile(scene.id + ".xml");
        state = juce::ValueTree::fromXml(file.loadFileAsString());
        if (state.hasType("CreationEngineScene"))
            return true;
        error = "Could not load the runtime fixture scene " + scene.id;
        return false;
    };
    source.readPods = [root](auto& catalog, auto& error) {
        const auto file = root.getChildFile("runtime-probe.frust");
        if (!file.existsAsFile()) {
            error = "Could not load the runtime fixture behavior.";
            return false;
        }
        catalog.GetOrCreateSource("runtime-probe", frust::PodKind::Behavior) = file.loadFileAsString();
        return true;
    };
    return source;
}

developer::Value RuntimeDiagnostics(const RuntimeWorldRunner& runner, engine::World& world, bool fixture)
{
    using developer::Object;
    auto diagnostics = Object({{"contextId", runner.ContextId()}, {"gameId", runner.Game().id},
                               {"sceneId", runner.Scene().id}, {"stepSeconds", runner.StepSeconds()},
                               {"runtimeError", runner.LastError()}});
    if (!fixture || runner.Game().id != "runtime-fixture")
        return diagnostics;

    std::lock_guard lock(world.RegistryMutex());
    auto& registry = world.Registry();
    const auto entities = registry.view<const scene::Name, const engine::Transform>();
    for (const auto entity : entities) {
        if (entities.get<const scene::Name>(entity).value != "Runtime Probe")
            continue;
        const auto& position = entities.get<const engine::Transform>(entity).position;
        auto sample = Object({{"x", position.x}, {"y", position.y}, {"z", position.z}});
        if (const auto* state = registry.try_get<scene::ObjectState>(entity)) {
            for (const auto& variable : state->values)
                sample.getDynamicObject()->setProperty(variable.name, variable.value);
        }
        diagnostics.getDynamicObject()->setProperty("fixtureProbe", sample);
        break;
    }
    return diagnostics;
}

} // namespace ce::runtime
