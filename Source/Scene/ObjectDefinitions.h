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
struct ObjectChildDefinition
{
    juce::String definitionId;
    engine::Transform localTransform;
};

// A reusable, data-first object recipe. Meshes and FRust behavior pods are
// referenced by identifier; the recipe owns no renderer or runtime object.
struct ObjectDefinition
{
    juce::String id;
    juce::String displayName;
    juce::String meshAssetId;
    juce::String meshAssetVersionId;
    juce::String meshPackId;
    juce::String meshPackVersion;
    engine::Transform initialTransform;
    juce::NamedValueSet defaultState;
    std::vector<juce::String> behaviorPods;
    std::vector<ObjectChildDefinition> children;
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
