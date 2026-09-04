#include "Scene/Components.h"
#include "Scene/ObjectDefinitions.h"

#include <iostream>

int main()
{
    ce::scene::ObjectDefinitionCatalog catalog;
    juce::String error;

    ce::scene::ObjectDefinition handle;
    handle.id = "door_handle";
    handle.displayName = "Door Handle";
    handle.initialTransform.position.x = 1.0f;
    if (!catalog.upsert(std::move(handle), error)) {
        std::cerr << error << '\n';
        return 1;
    }

    ce::scene::ObjectDefinition door;
    door.id = "oak_door";
    door.displayName = "Oak Door";
    door.defaultState.set("locked", true);

    ce::scene::ObjectComponentEntry meshComponent;
    meshComponent.kind = ce::scene::ObjectComponentKind::Mesh;
    meshComponent.meshAssetId = "castle_door";
    door.components.push_back(meshComponent);

    ce::scene::ObjectComponentEntry podComponent;
    podComponent.kind = ce::scene::ObjectComponentKind::Pod;
    podComponent.podId = "door_behavior";
    door.components.push_back(podComponent);

    ce::scene::ObjectComponentEntry childComponent;
    childComponent.kind = ce::scene::ObjectComponentKind::Child;
    childComponent.childDefinitionId = "door_handle";
    door.components.push_back(childComponent);
    if (!catalog.upsert(std::move(door), error)) {
        std::cerr << error << '\n';
        return 1;
    }

    ce::scene::ObjectDefinitionCatalog restored;
    if (!restored.restore(catalog.serialize(), error)) {
        std::cerr << error << '\n';
        return 1;
    }

    ce::engine::World world;
    const auto instance = ce::scene::ObjectFactory::instantiate(world, restored, "oak_door", { 10.0f, 0.0f, 0.0f }, error);
    if (instance.root == entt::null || instance.entities.size() != 2) {
        std::cerr << "Object definition did not instantiate its composed hierarchy: " << error << '\n';
        return 1;
    }

    const auto& registry = world.Registry();
    const auto& rootDefinition = registry.get<ce::scene::ObjectDefinitionRef>(instance.root);
    const auto& rootState = registry.get<ce::scene::ObjectState>(instance.root);
    const auto& rootBehaviors = registry.get<ce::scene::BehaviorAttachments>(instance.root);
    const auto& rootMesh = registry.get<ce::scene::MeshAssetReference>(instance.root);
    const auto child = instance.entities[1];
    const auto& childParent = registry.get<ce::scene::Parent>(child);
    const auto& childTransform = registry.get<ce::scene::Transform>(child);

    if (rootDefinition.definitionId != "oak_door" || !rootState.values.contains("locked") ||
        !static_cast<bool>(rootState.values["locked"]) || rootBehaviors.podIds != std::vector<juce::String>{ "door_behavior" } ||
        rootMesh.assetId != "castle_door" || childParent.value != instance.root || childTransform.position.x != 11.0f) {
        std::cerr << "Object definition instance lost authored identity, state, behavior, asset, or child composition." << '\n';
        return 1;
    }

    std::cout << "Creation Engine object definition composition passed." << '\n';
    return 0;
}
