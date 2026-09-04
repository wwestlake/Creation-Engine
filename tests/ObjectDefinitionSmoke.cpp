#include "Scene/Components.h"
#include "Scene/ObjectDefinitions.h"
#include "Scene/TransformHierarchy.h"

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
    // 3 entities now, not 2: oak_door's own root, its Mesh entry's child
    // entity (every Mesh entry is always its own child -- see
    // instantiateDefinition's own comment), and door_handle's root.
    if (instance.root == entt::null || instance.entities.size() != 3) {
        std::cerr << "Object definition did not instantiate its composed hierarchy: " << error << '\n';
        return 1;
    }

    const auto& registry = world.Registry();
    const auto& rootDefinition = registry.get<ce::scene::ObjectDefinitionRef>(instance.root);
    const auto& rootState = registry.get<ce::scene::ObjectState>(instance.root);
    const auto& rootBehaviors = registry.get<ce::scene::BehaviorAttachments>(instance.root);
    // The root itself carries no MeshAssetReference anymore -- every Mesh
    // entry is its own child entity now, uniformly, regardless of count.
    if (registry.try_get<ce::scene::MeshAssetReference>(instance.root) != nullptr) {
        std::cerr << "Object definition root should not carry a MeshAssetReference directly." << '\n';
        return 1;
    }

    entt::entity meshChild = entt::null, doorHandleChild = entt::null;
    for (const auto entity : instance.entities) {
        if (entity == instance.root) continue;
        if (registry.all_of<ce::scene::MeshAssetReference>(entity)) meshChild = entity;
        else doorHandleChild = entity;
    }
    if (meshChild == entt::null || doorHandleChild == entt::null) {
        std::cerr << "Could not find both the mesh child and the door_handle child." << '\n';
        return 1;
    }

    const auto& meshRef = registry.get<ce::scene::MeshAssetReference>(meshChild);
    const auto& meshParent = registry.get<ce::scene::Parent>(meshChild);
    const auto& meshTransform = registry.get<ce::scene::Transform>(meshChild);
    const auto& handleParent = registry.get<ce::scene::Parent>(doorHandleChild);
    const auto& handleTransform = registry.get<ce::scene::Transform>(doorHandleChild);
    const auto handleWorldX = ce::scene::MatrixTranslation(ce::scene::WorldModelMatrix(registry, doorHandleChild)).x;

    if (rootDefinition.definitionId != "oak_door" || !rootState.values.contains("locked") ||
        !static_cast<bool>(rootState.values["locked"]) || rootBehaviors.podIds != std::vector<juce::String>{ "door_behavior" } ||
        meshRef.assetId != "castle_door" || meshParent.value != instance.root ||
        // The mesh child's Transform must be the LOCAL value (identity,
        // since the test never set meshLocalTransform) -- NOT the drop
        // position (10,0,0) pre-baked in. That pre-baking was the actual
        // double-transform bug: WorldModelMatrix already composes this
        // child against the root's own (10,0,0) at query time, so baking
        // it in here too would double-count it.
        meshTransform.position.x != 0.0f || handleParent.value != instance.root ||
        // Same check for the nested Child entry: its own Transform stores
        // only its LOCAL offset (1.0, door_handle's own initialTransform),
        // not the drop position added in -- but the WORLD-composed value
        // (via WorldModelMatrix, walking Parent up to the root) must still
        // come out to 11.0 (10 dropped + 1 local), confirming the full
        // pipeline still produces the right answer end-to-end.
        handleTransform.position.x != 1.0f || handleWorldX != 11.0f) {
        std::cerr << "Object definition instance lost authored identity, state, behavior, asset, or child composition." << '\n';
        return 1;
    }

    std::cout << "Creation Engine object definition composition passed." << '\n';
    return 0;
}
