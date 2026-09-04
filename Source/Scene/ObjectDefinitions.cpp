#include "Scene/ObjectDefinitions.h"

#include <algorithm>
#include <unordered_set>

#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>
#include <creation/assets/ProjectSession.h>

#include "Scene/Components.h"

namespace ce::scene
{
namespace
{
juce::ValueTree serializeTransform(const engine::Transform& transform, const juce::Identifier& type)
{
    juce::ValueTree state(type);
    state.setProperty("posX", transform.position.x, nullptr);
    state.setProperty("posY", transform.position.y, nullptr);
    state.setProperty("posZ", transform.position.z, nullptr);
    state.setProperty("rotX", transform.eulerRotationRadians.x, nullptr);
    state.setProperty("rotY", transform.eulerRotationRadians.y, nullptr);
    state.setProperty("rotZ", transform.eulerRotationRadians.z, nullptr);
    state.setProperty("scaleX", transform.scale.x, nullptr);
    state.setProperty("scaleY", transform.scale.y, nullptr);
    state.setProperty("scaleZ", transform.scale.z, nullptr);
    return state;
}

engine::Transform restoreTransform(const juce::ValueTree& state)
{
    engine::Transform transform;
    transform.position = { static_cast<float>(state.getProperty("posX", 0.0)),
                           static_cast<float>(state.getProperty("posY", 0.0)),
                           static_cast<float>(state.getProperty("posZ", 0.0)) };
    transform.eulerRotationRadians = { static_cast<float>(state.getProperty("rotX", 0.0)),
                                       static_cast<float>(state.getProperty("rotY", 0.0)),
                                       static_cast<float>(state.getProperty("rotZ", 0.0)) };
    transform.scale = { static_cast<float>(state.getProperty("scaleX", 1.0)),
                        static_cast<float>(state.getProperty("scaleY", 1.0)),
                        static_cast<float>(state.getProperty("scaleZ", 1.0)) };
    return transform;
}

// One definition's ValueTree node -- factored out of serialize()/restore()
// so Save()/LoadAll() (one asset per definition) can reuse the exact same
// schema as the whole-catalog round-trip those already use, without a
// second parallel format.
juce::String ComponentKindToken(ObjectComponentKind kind)
{
    switch (kind) {
        case ObjectComponentKind::Mesh: return "Mesh";
        case ObjectComponentKind::Pod: return "Pod";
        case ObjectComponentKind::Child: return "Child";
    }
    return "Mesh";
}

juce::ValueTree SerializeOne(const ObjectDefinition& definition)
{
    juce::ValueTree node("ObjectDefinition");
    node.setProperty("id", definition.id, nullptr);
    node.setProperty("displayName", definition.displayName, nullptr);
    node.setProperty("editorOnly", definition.editorOnly, nullptr);
    node.addChild(serializeTransform(definition.initialTransform, "InitialTransform"), -1, nullptr);

    juce::ValueTree state("DefaultState");
    for (int index = 0; index < definition.defaultState.size(); ++index) {
        state.setProperty(definition.defaultState.getName(index), definition.defaultState.getValueAt(index), nullptr);
    }
    node.addChild(state, -1, nullptr);

    // One uniform list -- a mesh reference, a pod reference, and a child
    // object are the same kind of entry here, distinguished only by the
    // "kind" property. See docs/OBJECT_MODEL.md.
    juce::ValueTree components("Components");
    for (const auto& component : definition.components) {
        juce::ValueTree entry("Component");
        entry.setProperty("kind", ComponentKindToken(component.kind), nullptr);
        switch (component.kind) {
            case ObjectComponentKind::Mesh:
                entry.setProperty("meshAssetId", component.meshAssetId, nullptr);
                entry.setProperty("meshAssetVersionId", component.meshAssetVersionId, nullptr);
                entry.setProperty("meshPackId", component.meshPackId, nullptr);
                entry.setProperty("meshPackVersion", component.meshPackVersion, nullptr);
                entry.setProperty("meshNodeIndex", component.meshNodeIndex, nullptr);
                entry.setProperty("meshNodeName", component.meshNodeName, nullptr);
                entry.addChild(serializeTransform(component.meshLocalTransform, "MeshLocalTransform"), -1, nullptr);
                break;
            case ObjectComponentKind::Pod:
                entry.setProperty("podId", component.podId, nullptr);
                break;
            case ObjectComponentKind::Child:
                entry.setProperty("definitionId", component.childDefinitionId, nullptr);
                entry.addChild(serializeTransform(component.childLocalTransform, "LocalTransform"), -1, nullptr);
                break;
        }
        components.addChild(entry, -1, nullptr);
    }
    node.addChild(components, -1, nullptr);
    return node;
}

ObjectDefinition RestoreOne(const juce::ValueTree& node)
{
    ObjectDefinition definition;
    definition.id = node.getProperty("id").toString();
    definition.displayName = node.getProperty("displayName").toString();
    definition.editorOnly = static_cast<bool>(node.getProperty("editorOnly", false));
    definition.initialTransform = restoreTransform(node.getChildWithName("InitialTransform"));

    const auto defaultState = node.getChildWithName("DefaultState");
    for (int index = 0; index < defaultState.getNumProperties(); ++index) {
        definition.defaultState.set(defaultState.getPropertyName(index), defaultState.getProperty(defaultState.getPropertyName(index)));
    }

    for (const auto entry : node.getChildWithName("Components")) {
        if (!entry.hasType("Component")) continue;
        const auto kindToken = entry.getProperty("kind").toString();
        ObjectComponentEntry component;
        if (kindToken == "Mesh") {
            component.kind = ObjectComponentKind::Mesh;
            component.meshAssetId = entry.getProperty("meshAssetId").toString();
            component.meshAssetVersionId = entry.getProperty("meshAssetVersionId").toString();
            component.meshPackId = entry.getProperty("meshPackId").toString();
            component.meshPackVersion = entry.getProperty("meshPackVersion").toString();
            component.meshNodeIndex = static_cast<int>(entry.getProperty("meshNodeIndex", -1));
            component.meshNodeName = entry.getProperty("meshNodeName").toString();
            component.meshLocalTransform = restoreTransform(entry.getChildWithName("MeshLocalTransform"));
        } else if (kindToken == "Pod") {
            component.kind = ObjectComponentKind::Pod;
            component.podId = entry.getProperty("podId").toString();
        } else if (kindToken == "Child") {
            component.kind = ObjectComponentKind::Child;
            component.childDefinitionId = entry.getProperty("definitionId").toString();
            component.childLocalTransform = restoreTransform(entry.getChildWithName("LocalTransform"));
        } else {
            // Unrecognized kind (e.g. a stale pre-refactor asset with no
            // "kind" property at all) -- skip rather than fail the whole
            // definition. A stale asset degrades to missing components,
            // not a crash; see the plan's "No migration path" note.
            continue;
        }
        definition.components.push_back(std::move(component));
    }

    definition.id = definition.id.trim();
    if (definition.displayName.isEmpty()) {
        definition.displayName = definition.id;
    }
    return definition;
}

juce::String SlugifyDefinitionName(const juce::String& name) {
    juce::String slug;
    for (auto ch : name) {
        if (juce::CharacterFunctions::isLetterOrDigit(ch)) slug << juce::CharacterFunctions::toLowerCase(ch);
        else if (ch == ' ' || ch == '-' || ch == '_') slug << '-';
    }
    return slug.isEmpty() ? juce::Uuid().toString().replaceCharacter('-', '_') : slug;
}

juce::String LogicalPathFor(const juce::String& id) {
    return juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + "ObjectDefinitions/" +
           SlugifyDefinitionName(id) + ".objdef";
}

entt::entity instantiateDefinition(engine::World& world, const ObjectDefinitionCatalog& catalog,
                                   const ObjectDefinition& definition, const engine::Transform& parentTransform,
                                   entt::entity parent, std::unordered_set<std::string>& ancestry,
                                   ObjectInstantiationResult& result, juce::String& error)
{
    // This entity's own Transform component value -- `parentTransform`
    // composed with this definition's own authored `initialTransform`.
    // WorldModelMatrix (Source/Scene/TransformHierarchy.h) composes a
    // component's Transform against its Parent chain dynamically, at
    // render/pick/gizmo time. That means `parentTransform` must ALWAYS be
    // a purely LOCAL value relative to `parent` -- never something that
    // already has `parent`'s own world-equivalent transform baked into it,
    // or WorldModelMatrix double-counts it (composed once here, again at
    // query time). For the true root (ObjectFactory::instantiate's own
    // call, parent == entt::null) that local value legitimately IS the
    // real world placement, since there's nothing above it to compose
    // against. For a recursive Child call (below), it's
    // component.childLocalTransform alone -- see that call site's comment
    // for why NOT the calling entity's own `transform`, which was the
    // actual bug (identical in shape to the one just fixed for Mesh-kind
    // children).
    const auto transform = composeTransform(parentTransform, definition.initialTransform);
    const auto entity = world.CreateEntity();
    auto& registry = world.Registry();
    registry.emplace<Name>(entity, Name{ definition.displayName });
    registry.emplace<Transform>(entity, transform);
    SceneFlags flags;
    flags.editorOnly = definition.editorOnly;
    registry.emplace<SceneFlags>(entity, flags);
    registry.emplace<Parent>(entity, Parent{ parent });
    registry.emplace<ObjectDefinitionRef>(entity, ObjectDefinitionRef{ definition.id });
    registry.emplace<ObjectState>(entity, ObjectState{ definition.defaultState });

    // One pass over the uniform component list: Pod entries accumulate
    // into BehaviorAttachments (still unconditionally emplaced below, even
    // if empty -- matches prior behavior exactly). Every Mesh entry spawns
    // its own child entity -- deliberately uniform regardless of how many
    // Mesh entries there are (see docs/OBJECT_MODEL.md's "no privileged
    // tier": there is no principled reason exactly-one should be special-
    // cased to attach directly to `entity` while two-or-more don't; that
    // WAS the previous behavior and it was wrong -- an object with exactly
    // one part is not architecturally different from one with five).
    //
    // The child's Transform is `meshLocalTransform` AS-IS -- relative to
    // `entity`, not pre-composed against it. WorldModelMatrix (Source/
    // Scene/TransformHierarchy.h, used by every render/pick/gizmo call
    // site) walks the Parent chain and composes LOCAL transforms into a
    // world matrix at the moment it's needed; a component's own Transform
    // must always hold a value relative to its Parent, never a pre-baked
    // world value. Composing here (`composeTransform(transform,
    // meshLocalTransform)`) was a real bug: it baked `entity`'s position
    // into the child's Transform once at instantiate time, which
    // WorldModelMatrix then composed AGAINST `entity`'s transform a SECOND
    // time at render time -- double-counting it, and never correctly
    // reflecting the root moving afterward (a gizmo edit on the root
    // changed a value the child's already-baked position had no live
    // relationship to). Child (kind == Child) entries are recursed
    // separately, after this loop.
    BehaviorAttachments attachments;
    for (const auto& component : definition.components) {
        if (component.kind == ObjectComponentKind::Pod) {
            attachments.podIds.push_back(component.podId);
        } else if (component.kind == ObjectComponentKind::Mesh && component.meshAssetId.isNotEmpty()) {
            const auto meshEntity = world.CreateEntity();
            registry.emplace<Name>(meshEntity, Name{ component.meshNodeName.isNotEmpty() ? component.meshNodeName
                                                                                          : definition.displayName });
            registry.emplace<Transform>(meshEntity, component.meshLocalTransform);
            registry.emplace<SceneFlags>(meshEntity, flags);
            registry.emplace<Parent>(meshEntity, Parent{ entity });
            registry.emplace<MeshAssetReference>(meshEntity, MeshAssetReference{
                component.meshAssetId, component.meshAssetVersionId, component.meshPackId, component.meshPackVersion,
                component.meshNodeIndex, component.meshNodeName });
            result.entities.push_back(meshEntity);
        }
    }
    registry.emplace<BehaviorAttachments>(entity, std::move(attachments));
    result.entities.push_back(entity);

    ancestry.insert(definition.id.toStdString());
    for (const auto& component : definition.components) {
        if (component.kind != ObjectComponentKind::Child) continue;
        const auto childId = component.childDefinitionId.toStdString();
        if (ancestry.contains(childId)) {
            error = "Object definition cycle detected at '" + component.childDefinitionId + "'.";
            return entt::null;
        }
        const auto* childDefinition = catalog.find(component.childDefinitionId);
        if (childDefinition == nullptr) {
            error = "Object definition '" + definition.id + "' refers to missing child '" + component.childDefinitionId + "'.";
            return entt::null;
        }
        // Pass component.childLocalTransform ALONE -- the child's local
        // offset relative to `entity` -- not composed against `transform`
        // (this entity's own, now-correctly-local-or-world value). Parent{
        // entity} (set inside the recursive call, since `entity` is passed
        // as `parent` below) is what WorldModelMatrix uses to compose
        // against `entity`'s world position at query time; pre-mixing
        // `transform` in here too was exactly the double-bake bug just
        // fixed for Mesh-kind children, just one architectural layer up.
        if (instantiateDefinition(world, catalog, *childDefinition, component.childLocalTransform, entity, ancestry, result, error) == entt::null) {
            return entt::null;
        }
    }
    ancestry.erase(definition.id.toStdString());
    return entity;
}
} // namespace

engine::Transform composeTransform(const engine::Transform& parent, const engine::Transform& local)
{
    engine::Transform result = local;
    result.position.x += parent.position.x;
    result.position.y += parent.position.y;
    result.position.z += parent.position.z;
    result.eulerRotationRadians.x += parent.eulerRotationRadians.x;
    result.eulerRotationRadians.y += parent.eulerRotationRadians.y;
    result.eulerRotationRadians.z += parent.eulerRotationRadians.z;
    result.scale.x *= parent.scale.x;
    result.scale.y *= parent.scale.y;
    result.scale.z *= parent.scale.z;
    return result;
}

bool ObjectDefinitionCatalog::upsert(ObjectDefinition definition, juce::String& error)
{
    definition.id = definition.id.trim();
    definition.displayName = definition.displayName.trim();
    if (definition.id.isEmpty()) {
        error = "Object definitions need a stable identifier.";
        return false;
    }
    if (definition.displayName.isEmpty()) {
        definition.displayName = definition.id;
    }
    definitions[definition.id.toStdString()] = std::move(definition);
    return true;
}

const ObjectDefinition* ObjectDefinitionCatalog::find(const juce::String& id) const
{
    const auto found = definitions.find(id.toStdString());
    return found != definitions.end() ? &found->second : nullptr;
}

std::vector<juce::String> ObjectDefinitionCatalog::ids() const
{
    std::vector<juce::String> result;
    result.reserve(definitions.size());
    for (const auto& [id, definition] : definitions) {
        (void) definition;
        result.push_back(id);
    }
    return result;
}

juce::ValueTree ObjectDefinitionCatalog::serialize() const
{
    juce::ValueTree root("CreationEngineObjectDefinitions");
    std::vector<std::string> ids;
    ids.reserve(definitions.size());
    for (const auto& [id, definition] : definitions) {
        (void) definition;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());

    for (const auto& id : ids) {
        root.addChild(SerializeOne(definitions.at(id)), -1, nullptr);
    }
    return root;
}

bool ObjectDefinitionCatalog::restore(const juce::ValueTree& state, juce::String& error)
{
    if (!state.hasType("CreationEngineObjectDefinitions")) {
        error = "Object definition data has an unexpected root type.";
        return false;
    }

    std::unordered_map<std::string, ObjectDefinition> restored;
    for (const auto node : state) {
        if (!node.hasType("ObjectDefinition")) {
            continue;
        }
        ObjectDefinition definition = RestoreOne(node);
        if (definition.id.isEmpty()) {
            error = "Object definition data contains an empty identifier.";
            return false;
        }
        restored[definition.id.toStdString()] = std::move(definition);
    }
    definitions = std::move(restored);
    return true;
}

bool ObjectDefinitionCatalog::Save(creation::assets::ProjectSession& session, const juce::String& id, juce::String& error)
{
    const auto it = definitions.find(id.toStdString());
    if (it == definitions.end()) {
        error = "No Object Definition named \"" + id + "\" to save.";
        return false;
    }

    const auto node = SerializeOne(it->second);
    const auto xmlElement = node.createXml();
    if (!xmlElement) {
        error = "Could not serialize Object Definition \"" + id + "\".";
        return false;
    }
    const juce::String xml = xmlElement->toString();
    const juce::MemoryBlock data(xml.toRawUTF8(), xml.getNumBytesAsUTF8());

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::objectDefinition;
    options.displayName = it->second.displayName.isNotEmpty() ? it->second.displayName : it->second.id;
    options.logicalPath = LogicalPathFor(it->second.id);
    options.mediaType = "application/x-creation-engine-object-definition";
    options.sourceApp = "Creation Engine";
    options.sourceTool = "Object Definition Editor";
    options.description = "Creation Suite Object Definition";

    creation::assets::AssetDescriptor savedAsset;
    if (!creation::assets::ProjectAssetService::saveGeneratedAsset(session, data, options, savedAsset, error)) return false;

    if (!session.commit(error)) return false;
    return true;
}

bool ObjectDefinitionCatalog::LoadAll(creation::assets::ProjectSession& session, juce::String& error)
{
    const auto definitionAssets = session.getManifest().assetCatalog.query({ creation::assets::AssetKind::objectDefinition });

    for (const auto& asset : definitionAssets) {
        juce::MemoryBlock data;
        if (!session.readEntry(asset.logicalPath, data)) {
            error = "Could not read Object Definition asset \"" + asset.displayName + "\" (" + asset.logicalPath + ").";
            return false;
        }

        const auto parsedTree = juce::ValueTree::fromXml(data.toString());
        if (!parsedTree.isValid() || !parsedTree.hasType("ObjectDefinition")) {
            error = "Object Definition asset \"" + asset.displayName + "\" has a malformed payload.";
            return false;
        }

        ObjectDefinition definition = RestoreOne(parsedTree);
        if (definition.id.isEmpty()) {
            error = "Object Definition asset \"" + asset.displayName + "\" has no id.";
            return false;
        }
        definitions[definition.id.toStdString()] = std::move(definition);
    }

    return true;
}

ObjectInstantiationResult ObjectFactory::instantiate(engine::World& world, const ObjectDefinitionCatalog& catalog,
                                                     const juce::String& definitionId, engine::Vec3 position,
                                                     juce::String& error)
{
    const auto* definition = catalog.find(definitionId);
    if (definition == nullptr) {
        error = "Object definition '" + definitionId + "' was not found.";
        return {};
    }

    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    ObjectInstantiationResult result;
    engine::Transform rootTransform;
    rootTransform.position = position;
    std::unordered_set<std::string> ancestry;
    result.root = instantiateDefinition(world, catalog, *definition, rootTransform, entt::null, ancestry, result, error);
    if (result.root == entt::null) {
        for (const auto entity : result.entities) {
            if (world.Registry().valid(entity)) {
                world.DestroyEntity(entity);
            }
        }
        result.entities.clear();
    }
    return result;
}
} // namespace ce::scene
