#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "engine/core_components.h"
#include "engine/math.h"
#include "engine/world.h"

namespace creation::assets { class ProjectSession; }

namespace ce::scene
{
// See docs/OBJECT_MODEL.md: one primitive, "component," uniform and
// recursive -- no privileged tiers. A mesh reference, a behavior pod, and
// a nested child object are the SAME kind of list entry here, not three
// separately-typed fields (that was the previous, now-corrected shape of
// this struct). Tagged struct rather than std::variant -- std::variant is
// unused anywhere in this codebase, and juce::ValueTree serialization
// (used throughout this file) is naturally suited to an enum discriminant
// plus a superset of optional fields, not to std::variant.
enum class ObjectComponentKind
{
    Mesh,
    Pod,
    Child
};

struct ObjectComponentEntry
{
    ObjectComponentKind kind = ObjectComponentKind::Mesh;

    // kind == Mesh
    juce::String meshAssetId;
    juce::String meshAssetVersionId;
    juce::String meshPackId;
    juce::String meshPackVersion;

    // Which part of the source model this entry addresses -- see
    // docs/OBJECT_MODEL.md's "Multi-part import decomposes into
    // components" section. -1 means "whole model / first primitive",
    // today's exact single-mesh behavior; a multi-part import (one entry
    // per mesh-bearing node) sets this to that node's index. Addressing is
    // by index, not name -- meshNodeName is a display/fallback value only,
    // with the accepted order-dependence limitation the doc names.
    int meshNodeIndex = -1;
    juce::String meshNodeName;
    // This node's transform relative to the definition's root, precomputed
    // at import time by composing the node's parent chain (see
    // composeTransform). Identity for meshNodeIndex == -1.
    engine::Transform meshLocalTransform;

    // kind == Pod
    juce::String podId;

    // kind == Child
    juce::String childDefinitionId;
    engine::Transform childLocalTransform;
};

// A reusable, data-first object recipe: a name, a transform, default
// state, and a uniform list of components. The recipe owns no renderer or
// runtime object -- meshes and FRust behavior pods are referenced by
// identifier only, resolved at instantiation time.
struct ObjectDefinition
{
    juce::String id;
    juce::String displayName;
    engine::Transform initialTransform;
    juce::NamedValueSet defaultState;
    std::vector<ObjectComponentEntry> components;
    // Instances of this definition are excluded from Play (hidden, not
    // despawned) -- see scene::SceneFlags::editorOnly. VR Editor Cart
    // plan Phase 2 (the cart is the first thing that sets this true).
    bool editorOnly = false;
};

class ObjectDefinitionCatalog final
{
public:
    bool upsert(ObjectDefinition definition, juce::String& error);
    [[nodiscard]] const ObjectDefinition* find(const juce::String& id) const;
    [[nodiscard]] std::vector<juce::String> ids() const;
    [[nodiscard]] juce::ValueTree serialize() const;
    bool restore(const juce::ValueTree& state, juce::String& error);

    // Persists ONE definition (looked up by id) as its own AssetKind::
    // objectDefinition asset -- mirrors PodCatalog::Save exactly, same
    // one-asset-per-definition shape the Content Browser's per-kind
    // section expects (each definition is its own row, not one asset
    // holding the whole catalog).
    bool Save(creation::assets::ProjectSession& session, const juce::String& id, juce::String& error);

    // Inverse of Save -- queries every AssetKind::objectDefinition asset
    // in the project, reads and restores each one into `definitions`.
    // Existing in-memory definitions not backed by a persisted asset are
    // left untouched, same convention as PodCatalog::LoadAll.
    bool LoadAll(creation::assets::ProjectSession& session, juce::String& error);

private:
    std::unordered_map<std::string, ObjectDefinition> definitions;
};

// Composes a child's transform relative to its parent's world transform.
// Additive, not a real matrix composition (see docs/OBJECT_MODEL.md's
// "Multi-part import decomposes into components" section for the accepted
// limitation this implies) -- shared by ObjectFactory::instantiate and
// GltfAssetImporter's node-parent-chain walk so both use identical math.
engine::Transform composeTransform(const engine::Transform& parent, const engine::Transform& child);

struct ObjectInstantiationResult
{
    entt::entity root = entt::null;
    std::vector<entt::entity> entities;
};

class ObjectFactory final
{
public:
    static ObjectInstantiationResult instantiate(engine::World& world, const ObjectDefinitionCatalog& catalog,
                                                  const juce::String& definitionId, engine::Vec3 position,
                                                  juce::String& error);
};
} // namespace ce::scene
