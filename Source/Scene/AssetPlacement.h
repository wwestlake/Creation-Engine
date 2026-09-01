#pragma once

#include <JuceHeader.h>

#include "engine/world.h"
#include "Scene/AssetCatalog.h"

namespace ce::scene {

// Places a new entity referencing `asset`'s mesh/material (and skeleton/
// animation clips, if the asset has them) into `world` -- the exact
// component shape a placed 3D asset needs: Transform + MeshRenderer +
// SceneFlags + Parent, plus Skeleton/Animator when present. Previously
// this lived only inside HierarchyPanel::AddEntity (the "+ Add" menu);
// pulled out here so importers can place a dropped model straight into
// the scene too, without a second, drifting copy of this component list.
//
// Mirrors HierarchyPanel::AddEntity's own component construction exactly
// -- see that function's comments for why Skeleton is copied (per-
// instance joint hierarchy) while animationClips is shared (asset-level
// clip data).
//
// Caller must already hold world.RegistryMutex() -- CreateEntity() and
// every emplace<> below assume the lock is held, same convention as
// every other multi-component World mutation in this codebase (World's
// own header comment explains why the lock exists).
entt::entity PlaceAssetEntity(engine::World& world, const AssetCatalog::Asset& asset, const juce::String& name,
                               engine::Vec3 position, entt::entity parent = entt::null,
                               const juce::String& durableAssetId = {}, const juce::String& durableVersionId = {});

} // namespace ce::scene
