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

// One definition's ValueTree node -- factored out of serialize()/restore()
// so Save()/LoadAll() (one asset per definition) can reuse the exact same
// schema as the whole-catalog round-trip those already use, without a
// second parallel format.
juce::ValueTree SerializeOne(const ObjectDefinition& definition)
{
    juce::ValueTree node("ObjectDefinition");
    node.setProperty("id", definition.id, nullptr);
    node.setProperty("displayName", definition.displayName, nullptr);
    node.setProperty("meshAssetId", definition.meshAssetId, nullptr);
    node.setProperty("meshAssetVersionId", definition.meshAssetVersionId, nullptr);
    node.setProperty("meshPackId", definition.meshPackId, nullptr);
    node.setProperty("meshPackVersion", definition.meshPackVersion, nullptr);
    node.addChild(serializeTransform(definition.initialTransform, "InitialTransform"), -1, nullptr);

    juce::ValueTree state("DefaultState");
    for (int index = 0; index < definition.defaultState.size(); ++index) {
        state.setProperty(definition.defaultState.getName(index), definition.defaultState.getValueAt(index), nullptr);
    }
    node.addChild(state, -1, nullptr);

    juce::ValueTree behaviors("Behaviors");
    for (const auto& podId : definition.behaviorPods) {
        juce::ValueTree behavior("Behavior");
        behavior.setProperty("podId", podId, nullptr);
        behaviors.addChild(behavior, -1, nullptr);
    }
    node.addChild(behaviors, -1, nullptr);

    juce::ValueTree children("Children");
    for (const auto& child : definition.children) {
        juce::ValueTree childNode("Child");
        childNode.setProperty("definitionId", child.definitionId, nullptr);
        childNode.addChild(serializeTransform(child.localTransform, "LocalTransform"), -1, nullptr);
        children.addChild(childNode, -1, nullptr);
    }
    node.addChild(children, -1, nullptr);
    return node;
}

ObjectDefinition RestoreOne(const juce::ValueTree& node)
{
    ObjectDefinition definition;
    definition.id = node.getProperty("id").toString();
    definition.displayName = node.getProperty("displayName").toString();
    definition.meshAssetId = node.getProperty("meshAssetId").toString();
    definition.meshAssetVersionId = node.getProperty("meshAssetVersionId").toString();
    definition.meshPackId = node.getProperty("meshPackId").toString();
    definition.meshPackVersion = node.getProperty("meshPackVersion").toString();
    definition.initialTransform = restoreTransform(node.getChildWithName("InitialTransform"));

    const auto defaultState = node.getChildWithName("DefaultState");
    for (int index = 0; index < defaultState.getNumProperties(); ++index) {
        definition.defaultState.set(defaultState.getPropertyName(index), defaultState.getProperty(defaultState.getPropertyName(index)));
    }

    for (const auto behavior : node.getChildWithName("Behaviors")) {
        if (behavior.hasType("Behavior")) {
            definition.behaviorPods.push_back(behavior.getProperty("podId").toString());
        }
    }
    for (const auto child : node.getChildWithName("Children")) {
        if (child.hasType("Child")) {
            definition.children.push_back({ child.getProperty("definitionId").toString(),
                                            restoreTransform(child.getChildWithName("LocalTransform")) });
        }
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
    const auto transform = composeTransform(parentTransform, definition.initialTransform);
    const auto entity = world.CreateEntity();
    auto& registry = world.Registry();
    registry.emplace<Name>(entity, Name{ definition.displayName });
    registry.emplace<Transform>(entity, transform);
    registry.emplace<SceneFlags>(entity, SceneFlags{});
    registry.emplace<Parent>(entity, Parent{ parent });
    registry.emplace<ObjectDefinitionRef>(entity, ObjectDefinitionRef{ definition.id });
    registry.emplace<ObjectState>(entity, ObjectState{ definition.defaultState });
    registry.emplace<BehaviorAttachments>(entity, BehaviorAttachments{ definition.behaviorPods });
    if (definition.meshAssetId.isNotEmpty()) {
        registry.emplace<MeshAssetReference>(entity, MeshAssetReference{
            definition.meshAssetId, definition.meshAssetVersionId, definition.meshPackId, definition.meshPackVersion });
    }
    result.entities.push_back(entity);

    ancestry.insert(definition.id.toStdString());
    for (const auto& child : definition.children) {
        const auto childId = child.definitionId.toStdString();
        if (ancestry.contains(childId)) {
            error = "Object definition cycle detected at '" + child.definitionId + "'.";
            return entt::null;
        }
        const auto* childDefinition = catalog.find(child.definitionId);
        if (childDefinition == nullptr) {
            error = "Object definition '" + definition.id + "' refers to missing child '" + child.definitionId + "'.";
            return entt::null;
        }
        const auto childParentTransform = composeTransform(transform, child.localTransform);
        if (instantiateDefinition(world, catalog, *childDefinition, childParentTransform, entity, ancestry, result, error) == entt::null) {
            return entt::null;
        }
    }
    ancestry.erase(definition.id.toStdString());
    return entity;
}
} // namespace

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
