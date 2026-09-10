#include "Scene/Components.h"
#include "Scene/ObjectDefinitions.h"
#include "Scene/TransformHierarchy.h"

#include <iostream>

int main()
{
    ce::scene::ObjectDefinitionCatalog catalog;
    juce::String error;

    ce::scene::ObjectDefinition childAssembly;
    childAssembly.id = "child_assembly";
    childAssembly.displayName = "Child Assembly";
    childAssembly.initialTransform.position.x = 1.0f;
    if (!catalog.upsert(std::move(childAssembly), error)) {
        std::cerr << error << '\n';
        return 1;
    }

    ce::scene::ObjectDefinition assembly;
    assembly.id = "assembly_alpha";
    assembly.displayName = "Assembly Alpha";
    assembly.defaultState.set("enabled", true);

    ce::scene::ObjectComponentEntry meshComponent;
    meshComponent.componentInstanceId = "mesh-component";
    meshComponent.kind = ce::scene::ObjectComponentKind::Mesh;
    meshComponent.meshAssetId = "mesh-asset";
    assembly.components.push_back(meshComponent);

    ce::scene::ObjectComponentEntry podComponent;
    podComponent.componentInstanceId = "pod-component";
    podComponent.kind = ce::scene::ObjectComponentKind::Pod;
    podComponent.podId = "behavior-pod";
    assembly.components.push_back(podComponent);

    ce::scene::ObjectComponentEntry childComponent;
    childComponent.componentInstanceId = "child-component";
    childComponent.kind = ce::scene::ObjectComponentKind::Child;
    childComponent.childDefinitionId = "child_assembly";
    assembly.components.push_back(childComponent);

    assembly.connections.push_back({ "pod-component", "output", "mesh-component", "property" });
    if (!catalog.upsert(std::move(assembly), error)) {
        std::cerr << error << '\n';
        return 1;
    }

    ce::scene::ObjectDefinition invalidAssembly;
    invalidAssembly.id = "invalid-assembly";
    invalidAssembly.components.push_back({ "known-component" });
    invalidAssembly.connections.push_back({ "known-component", "out", "missing-component", "in" });
    if (catalog.upsert(std::move(invalidAssembly), error)) {
        std::cerr << "Accepted a connection to a missing component." << '\n';
        return 1;
    }

    ce::scene::ObjectDefinitionCatalog restored;
    if (!restored.restore(catalog.serialize(), error)) {
        std::cerr << error << '\n';
        return 1;
    }

    const auto* restoredAssembly = restored.find("assembly_alpha");
    if (restoredAssembly == nullptr || restoredAssembly->connections.size() != 1 ||
        restoredAssembly->connections.front().sourceComponentInstanceId != "pod-component" ||
        restoredAssembly->connections.front().targetComponentInstanceId != "mesh-component") {
        std::cerr << "Component assembly connection did not round-trip." << '\n';
        return 1;
    }

    ce::engine::World world;
    const auto instance = ce::scene::ObjectFactory::instantiate(world, restored, "assembly_alpha", { 10.0f, 0.0f, 0.0f }, error);
    // 3 entities now, not 2: the assembly root, its Mesh entry's child
    // entity (every Mesh entry is always its own child -- see
    // instantiateDefinition's own comment), and the child assembly's root.
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

    entt::entity meshChild = entt::null, childAssemblyEntity = entt::null;
    for (const auto entity : instance.entities) {
        if (entity == instance.root) continue;
        if (registry.all_of<ce::scene::MeshAssetReference>(entity)) meshChild = entity;
        else childAssemblyEntity = entity;
    }
    if (meshChild == entt::null || childAssemblyEntity == entt::null) {
        std::cerr << "Could not find both the mesh child and the child assembly." << '\n';
        return 1;
    }

    const auto& meshRef = registry.get<ce::scene::MeshAssetReference>(meshChild);
    const auto& meshParent = registry.get<ce::scene::Parent>(meshChild);
    const auto& meshTransform = registry.get<ce::scene::Transform>(meshChild);
    const auto& childParent = registry.get<ce::scene::Parent>(childAssemblyEntity);
    const auto& childTransform = registry.get<ce::scene::Transform>(childAssemblyEntity);
    const auto childWorldX = ce::scene::MatrixTranslation(ce::scene::WorldModelMatrix(registry, childAssemblyEntity)).x;

    if (rootDefinition.definitionId != "assembly_alpha" || !rootState.values.contains("enabled") ||
        !static_cast<bool>(rootState.values["enabled"]) || rootBehaviors.podIds != std::vector<juce::String>{ "behavior-pod" } ||
        meshRef.assetId != "mesh-asset" || meshParent.value != instance.root ||
        // The mesh child's Transform must be the LOCAL value (identity,
        // since the test never set meshLocalTransform) -- NOT the drop
        // position (10,0,0) pre-baked in. That pre-baking was the actual
        // double-transform bug: WorldModelMatrix already composes this
        // child against the root's own (10,0,0) at query time, so baking
        // it in here too would double-count it.
        meshTransform.position.x != 0.0f || handleParent.value != instance.root ||
        // Same check for the nested Child entry: its own Transform stores
        // only its LOCAL offset (1.0, the child assembly's initialTransform),
        // not the drop position added in -- but the WORLD-composed value
        // (via WorldModelMatrix, walking Parent up to the root) must still
        // come out to 11.0 (10 dropped + 1 local), confirming the full
        // pipeline still produces the right answer end-to-end.
        childTransform.position.x != 1.0f || childWorldX != 11.0f) {
        std::cerr << "Object definition instance lost authored identity, state, behavior, asset, or child composition." << '\n';
        return 1;
    }

    std::cout << "Creation Engine object definition composition passed." << '\n';
    return 0;
}
