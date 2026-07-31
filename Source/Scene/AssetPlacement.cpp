#include "Scene/AssetPlacement.h"

#include "Scene/Components.h"

namespace ce::scene {

entt::entity PlaceAssetEntity(engine::World& world, const AssetCatalog::Asset& asset, const juce::String& name,
                               engine::Vec3 position, entt::entity parent) {
    auto& registry = world.Registry();

    const entt::entity newEntity = world.CreateEntity();
    registry.emplace<scene::Name>(newEntity, scene::Name{ name });
    registry.emplace<scene::Transform>(newEntity, scene::Transform{ position });
    registry.emplace<scene::MeshRenderer>(newEntity, scene::MeshRenderer{ asset.mesh, asset.material });
    registry.emplace<scene::SceneFlags>(newEntity, scene::SceneFlags{});
    registry.emplace<scene::Parent>(newEntity, scene::Parent{ parent });
    if (asset.skeleton != nullptr) {
        // Copied, not shared -- this entity owns an independent joint
        // hierarchy from here on (see HierarchyPanel::AddEntity's own
        // comment, which this mirrors).
        registry.emplace<scene::Skeleton>(newEntity, *asset.skeleton);
    }
    if (asset.animationClips != nullptr && !asset.animationClips->empty()) {
        registry.emplace<scene::Animator>(newEntity, scene::Animator{ asset.animationClips, 0, 0.0f, false, true });
    }

    return newEntity;
}

} // namespace ce::scene
