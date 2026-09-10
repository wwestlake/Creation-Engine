#include "Scene/EngineSceneSerializer.h"

#include "Physics/PhysicsComponents.h"
#include "Render/Scene/Material.h"

namespace ce::scene
{
juce::ValueTree EngineSceneSerializer::serializeScene(ce::engine::World& world)
{
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    auto& reg = world.Registry();

    juce::ValueTree root("CreationEngineScene");
    juce::ValueTree entitiesNode("Entities");

    for (auto entity : reg.storage<entt::entity>())
    {
        // The Scene's own root entity is synthesized fresh on every load,
        // never persisted -- see SceneRoot's own header comment.
        if (reg.all_of<SceneRoot>(entity)) continue;

        juce::ValueTree entityNode("Entity");
        entityNode.setProperty("id", static_cast<int64_t>(entity), nullptr);

        if (auto* name = reg.try_get<Name>(entity))
            entityNode.setProperty("name", name->value, nullptr);

        // Scene/Game VFS Rearchitecture plan, Phase 1: InstanceId is minted
        // once at placement (AssetPlacement.cpp/ObjectDefinitions.cpp) but
        // was never actually persisted here despite that -- silently
        // dangling every reference keyed by it (Pod-to-scene-instance
        // references) the moment a project was saved and reopened. The
        // synthetic SceneRoot/purely-transient entities that never get one
        // in the first place naturally skip this the same way they always
        // have (try_get returns nullptr).
        if (auto* instanceId = reg.try_get<InstanceId>(entity))
            entityNode.setProperty("instanceId", instanceId->value, nullptr);

        if (auto* parent = reg.try_get<Parent>(entity))
        {
            // A Parent pointing at the synthetic SceneRoot isn't real
            // information to persist -- that entity doesn't survive this
            // save, and being unparented is exactly what re-attaches an
            // entity to the (freshly synthesized) root on next load
            // anyway. Only a genuine, persisted parent is worth writing.
            if (parent->value != entt::null && !reg.all_of<SceneRoot>(parent->value))
                entityNode.setProperty("parentId", static_cast<int64_t>(parent->value), nullptr);
        }

        if (reg.all_of<Folder>(entity))
            entityNode.setProperty("isFolder", true, nullptr);

        if (auto* flags = reg.try_get<SceneFlags>(entity))
        {
            entityNode.setProperty("visible", flags->visible, nullptr);
            entityNode.setProperty("locked", flags->locked, nullptr);
            entityNode.setProperty("editorOnly", flags->editorOnly, nullptr);
        }

        if (auto* transform = reg.try_get<Transform>(entity))
        {
            juce::ValueTree tNode("Transform");
            tNode.setProperty("posX", transform->position.x, nullptr);
            tNode.setProperty("posY", transform->position.y, nullptr);
            tNode.setProperty("posZ", transform->position.z, nullptr);
            tNode.setProperty("rotX", transform->eulerRotationRadians.x, nullptr);
            tNode.setProperty("rotY", transform->eulerRotationRadians.y, nullptr);
            tNode.setProperty("rotZ", transform->eulerRotationRadians.z, nullptr);
            tNode.setProperty("scaleX", transform->scale.x, nullptr);
            tNode.setProperty("scaleY", transform->scale.y, nullptr);
            tNode.setProperty("scaleZ", transform->scale.z, nullptr);
            entityNode.addChild(tNode, -1, nullptr);
        }

        if (auto* meshRenderer = reg.try_get<MeshRenderer>(entity))
        {
            if (meshRenderer->material != nullptr)
            {
                juce::ValueTree mNode("Material");
                mNode.setProperty("albedoR", meshRenderer->material->albedo.x, nullptr);
                mNode.setProperty("albedoG", meshRenderer->material->albedo.y, nullptr);
                mNode.setProperty("albedoB", meshRenderer->material->albedo.z, nullptr);
                mNode.setProperty("metallic", meshRenderer->material->metallic, nullptr);
                mNode.setProperty("roughness", meshRenderer->material->roughness, nullptr);
                entityNode.addChild(mNode, -1, nullptr);
            }
        }

        if (auto* meshAsset = reg.try_get<MeshAssetReference>(entity))
        {
            entityNode.setProperty("meshAssetId", meshAsset->assetId, nullptr);
            entityNode.setProperty("meshAssetVersionId", meshAsset->versionId, nullptr);
            entityNode.setProperty("meshPackId", meshAsset->packId, nullptr);
            entityNode.setProperty("meshPackVersion", meshAsset->packVersion, nullptr);
            entityNode.setProperty("meshNodeIndex", meshAsset->nodeIndex, nullptr);
            entityNode.setProperty("meshNodeName", meshAsset->nodeName, nullptr);
        }

        if (auto* tint = reg.try_get<engine::Tint>(entity))
        {
            juce::ValueTree tintNode("Tint");
            tintNode.setProperty("r", tint->color.x, nullptr);
            tintNode.setProperty("g", tint->color.y, nullptr);
            tintNode.setProperty("b", tint->color.z, nullptr);
            entityNode.addChild(tintNode, -1, nullptr);
        }

        if (auto* objectDefinition = reg.try_get<ObjectDefinitionRef>(entity))
            entityNode.setProperty("objectDefinitionId", objectDefinition->definitionId, nullptr);

        if (auto* character = reg.try_get<CharacterInstanceRef>(entity))
        {
            juce::ValueTree characterNode("CharacterInstance");
            characterNode.setProperty("instanceId", character->instanceId, nullptr);
            characterNode.setProperty("definitionAssetId", character->definitionAssetId, nullptr);
            characterNode.setProperty("definitionVersionId", character->definitionVersionId, nullptr);
            characterNode.setProperty("rosterAssetId", character->rosterAssetId, nullptr);
            for (int index = 0; index < character->state.size(); ++index)
                characterNode.setProperty(character->state.getName(index), character->state.getValueAt(index), nullptr);
            entityNode.addChild(characterNode, -1, nullptr);
        }

        if (auto* possessionSpawn = reg.try_get<PossessionSpawn>(entity)) {
            entityNode.setProperty("possessionPlayerSlotId", possessionSpawn->playerSlotId, nullptr);
            entityNode.setProperty("possessionCharacterAssetId", possessionSpawn->characterAssetId, nullptr);
        }

        if (auto* builtIn = reg.try_get<SceneBuiltIn>(entity))
            entityNode.setProperty("builtInKind", static_cast<int>(builtIn->kind), nullptr);

        if (auto* spawner = reg.try_get<Spawner>(entity))
        {
            juce::ValueTree spawnerNode("Spawner");
            spawnerNode.setProperty("spawnId", spawner->spawnId, nullptr);
            spawnerNode.setProperty("enabled", spawner->enabled, nullptr);
            spawnerNode.setProperty("maximumActive", spawner->maximumActive, nullptr);
            spawnerNode.setProperty("respawnDelaySeconds", spawner->respawnDelaySeconds, nullptr);
            entityNode.addChild(spawnerNode, -1, nullptr);
        }

        if (auto* playerSpawn = reg.try_get<PlayerSpawn>(entity))
        {
            entityNode.setProperty("isPlayerSpawn", true, nullptr);
            entityNode.setProperty("playerCapsuleRadiusMeters", playerSpawn->capsuleRadiusMeters, nullptr);
            entityNode.setProperty("playerCapsuleHalfHeightMeters", playerSpawn->capsuleHalfHeightMeters, nullptr);
        }

        if (auto* rigidBody = reg.try_get<physics::RigidBodyComponent>(entity))
        {
            juce::ValueTree rigidBodyNode("RigidBody");
            rigidBodyNode.setProperty("motionType", static_cast<int>(rigidBody->motionType), nullptr);
            rigidBodyNode.setProperty("mass", rigidBody->mass, nullptr);
            rigidBodyNode.setProperty("friction", rigidBody->friction, nullptr);
            rigidBodyNode.setProperty("restitution", rigidBody->restitution, nullptr);
            rigidBodyNode.setProperty("linearDamping", rigidBody->linearDamping, nullptr);
            rigidBodyNode.setProperty("angularDamping", rigidBody->angularDamping, nullptr);
            entityNode.addChild(rigidBodyNode, -1, nullptr);
        }

        if (auto* collider = reg.try_get<physics::ColliderComponent>(entity))
        {
            juce::ValueTree colliderNode("Collider");
            colliderNode.setProperty("shape", static_cast<int>(collider->shape), nullptr);
            colliderNode.setProperty("halfExtentX", collider->halfExtentX, nullptr);
            colliderNode.setProperty("halfExtentY", collider->halfExtentY, nullptr);
            colliderNode.setProperty("halfExtentZ", collider->halfExtentZ, nullptr);
            colliderNode.setProperty("radius", collider->radius, nullptr);
            colliderNode.setProperty("halfHeight", collider->halfHeight, nullptr);
            colliderNode.setProperty("collisionLayer", static_cast<int>(collider->collisionLayer), nullptr);
            colliderNode.setProperty("isSensor", collider->isSensor, nullptr);
            entityNode.addChild(colliderNode, -1, nullptr);
        }

        if (auto* behaviors = reg.try_get<BehaviorAttachments>(entity))
        {
            juce::ValueTree behaviorNode("Behaviors");
            for (const auto& podId : behaviors->podIds)
            {
                juce::ValueTree podNode("Behavior");
                podNode.setProperty("podId", podId, nullptr);
                behaviorNode.addChild(podNode, -1, nullptr);
            }
            entityNode.addChild(behaviorNode, -1, nullptr);
        }

        if (auto* objectState = reg.try_get<ObjectState>(entity))
        {
            juce::ValueTree stateNode("ObjectState");
            for (int index = 0; index < objectState->values.size(); ++index)
                stateNode.setProperty(objectState->values.getName(index), objectState->values.getValueAt(index), nullptr);
            entityNode.addChild(stateNode, -1, nullptr);
        }

        entitiesNode.addChild(entityNode, -1, nullptr);
    }

    root.addChild(entitiesNode, -1, nullptr);
    return root;
}

bool EngineSceneSerializer::restoreScene(ce::engine::World& world, const juce::ValueTree& state)
{
    if (! state.hasType("CreationEngineScene"))
        return false;

    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    auto& reg = world.Registry();
    reg.clear();

    auto entitiesNode = state.getChildWithName("Entities");
    if (! entitiesNode.isValid())
        return true;

    std::map<int64_t, entt::entity> idMap;

    for (const auto entityNode : entitiesNode)
    {
        if (! entityNode.hasType("Entity"))
            continue;

        int64_t savedId = entityNode.getProperty("id", -1);
        auto entity = reg.create();
        if (savedId >= 0)
            idMap[savedId] = entity;

        auto nameText = entityNode.getProperty("name").toString();
        if (nameText.isNotEmpty())
            reg.emplace<Name>(entity, nameText);

        const auto instanceIdText = entityNode.getProperty("instanceId").toString();
        if (instanceIdText.isNotEmpty())
            reg.emplace<InstanceId>(entity, InstanceId{ instanceIdText });

        if (entityNode.getProperty("isFolder", false))
            reg.emplace<Folder>(entity);

        SceneFlags flags;
        flags.visible = entityNode.getProperty("visible", true);
        flags.locked = entityNode.getProperty("locked", false);
        flags.editorOnly = entityNode.getProperty("editorOnly", false);
        reg.emplace<SceneFlags>(entity, flags);

        auto tNode = entityNode.getChildWithName("Transform");
        if (tNode.isValid())
        {
            Transform t;
            t.position.x = static_cast<float>(tNode.getProperty("posX", 0.0));
            t.position.y = static_cast<float>(tNode.getProperty("posY", 0.0));
            t.position.z = static_cast<float>(tNode.getProperty("posZ", 0.0));
            t.eulerRotationRadians.x = static_cast<float>(tNode.getProperty("rotX", 0.0));
            t.eulerRotationRadians.y = static_cast<float>(tNode.getProperty("rotY", 0.0));
            t.eulerRotationRadians.z = static_cast<float>(tNode.getProperty("rotZ", 0.0));
            t.scale.x = static_cast<float>(tNode.getProperty("scaleX", 1.0));
            t.scale.y = static_cast<float>(tNode.getProperty("scaleY", 1.0));
            t.scale.z = static_cast<float>(tNode.getProperty("scaleZ", 1.0));
            reg.emplace<Transform>(entity, t);
        }

        const auto meshAssetId = entityNode.getProperty("meshAssetId").toString();
        if (meshAssetId.isNotEmpty())
            reg.emplace<MeshAssetReference>(entity, MeshAssetReference{
                meshAssetId,
                entityNode.getProperty("meshAssetVersionId").toString(),
                entityNode.getProperty("meshPackId").toString(),
                entityNode.getProperty("meshPackVersion").toString(),
                static_cast<int>(entityNode.getProperty("meshNodeIndex", -1)),
                entityNode.getProperty("meshNodeName").toString() });

        if (const auto tintNode = entityNode.getChildWithName("Tint"); tintNode.isValid())
        {
            engine::Tint tint;
            tint.color.x = static_cast<float>(tintNode.getProperty("r", 1.0));
            tint.color.y = static_cast<float>(tintNode.getProperty("g", 1.0));
            tint.color.z = static_cast<float>(tintNode.getProperty("b", 1.0));
            reg.emplace<engine::Tint>(entity, tint);
        }

        const auto objectDefinitionId = entityNode.getProperty("objectDefinitionId").toString();
        if (objectDefinitionId.isNotEmpty())
            reg.emplace<ObjectDefinitionRef>(entity, ObjectDefinitionRef{ objectDefinitionId });

        if (const auto characterNode = entityNode.getChildWithName("CharacterInstance"); characterNode.isValid())
        {
            CharacterInstanceRef character;
            character.instanceId = characterNode.getProperty("instanceId").toString();
            character.definitionAssetId = characterNode.getProperty("definitionAssetId").toString();
            character.definitionVersionId = characterNode.getProperty("definitionVersionId").toString();
            character.rosterAssetId = characterNode.getProperty("rosterAssetId").toString();
            for (int index = 0; index < characterNode.getNumProperties(); ++index)
            {
                const auto property = characterNode.getPropertyName(index);
                if (property != juce::Identifier("instanceId") && property != juce::Identifier("definitionAssetId") &&
                    property != juce::Identifier("definitionVersionId") && property != juce::Identifier("rosterAssetId"))
                    character.state.set(property, characterNode.getProperty(property));
            }
            reg.emplace<CharacterInstanceRef>(entity, std::move(character));
        }

        const auto possessionSlotId = entityNode.getProperty("possessionPlayerSlotId").toString();
        if (possessionSlotId.isNotEmpty())
            reg.emplace<PossessionSpawn>(entity, PossessionSpawn{
                possessionSlotId, entityNode.getProperty("possessionCharacterAssetId").toString() });

        if (entityNode.hasProperty("builtInKind"))
            reg.emplace<SceneBuiltIn>(entity, SceneBuiltIn{
                static_cast<BuiltInKind>(static_cast<int>(entityNode.getProperty("builtInKind"))) });

        if (const auto spawnerNode = entityNode.getChildWithName("Spawner"); spawnerNode.isValid())
        {
            Spawner spawner;
            spawner.spawnId = spawnerNode.getProperty("spawnId").toString();
            spawner.enabled = spawnerNode.getProperty("enabled", true);
            spawner.maximumActive = static_cast<int>(spawnerNode.getProperty("maximumActive", 1));
            spawner.respawnDelaySeconds = static_cast<float>(spawnerNode.getProperty("respawnDelaySeconds", 0.0));
            reg.emplace<Spawner>(entity, std::move(spawner));
        }

        if (entityNode.getProperty("isPlayerSpawn", false))
        {
            PlayerSpawn playerSpawn;
            playerSpawn.capsuleRadiusMeters = static_cast<float>(entityNode.getProperty("playerCapsuleRadiusMeters", 0.3));
            playerSpawn.capsuleHalfHeightMeters = static_cast<float>(entityNode.getProperty("playerCapsuleHalfHeightMeters", 0.9));
            reg.emplace<PlayerSpawn>(entity, playerSpawn);
            // One-way migration at load time: legacy scenes become usable by
            // the slot/spawn runtime without rewriting their original data.
            if (!reg.all_of<PossessionSpawn>(entity))
                reg.emplace<PossessionSpawn>(entity, PossessionSpawn{});
        }

        if (const auto rigidBodyNode = entityNode.getChildWithName("RigidBody"); rigidBodyNode.isValid())
        {
            physics::RigidBodyComponent rigidBody;
            rigidBody.motionType = static_cast<physics::MotionType>(static_cast<int>(rigidBodyNode.getProperty("motionType", 2)));
            rigidBody.mass = static_cast<float>(rigidBodyNode.getProperty("mass", 1.0));
            rigidBody.friction = static_cast<float>(rigidBodyNode.getProperty("friction", 0.5));
            rigidBody.restitution = static_cast<float>(rigidBodyNode.getProperty("restitution", 0.0));
            rigidBody.linearDamping = static_cast<float>(rigidBodyNode.getProperty("linearDamping", 0.05));
            rigidBody.angularDamping = static_cast<float>(rigidBodyNode.getProperty("angularDamping", 0.05));
            reg.emplace<physics::RigidBodyComponent>(entity, rigidBody);
        }

        if (const auto colliderNode = entityNode.getChildWithName("Collider"); colliderNode.isValid())
        {
            physics::ColliderComponent collider;
            collider.shape = static_cast<physics::ColliderShapeKind>(static_cast<int>(colliderNode.getProperty("shape", 0)));
            collider.halfExtentX = static_cast<float>(colliderNode.getProperty("halfExtentX", 0.5));
            collider.halfExtentY = static_cast<float>(colliderNode.getProperty("halfExtentY", 0.5));
            collider.halfExtentZ = static_cast<float>(colliderNode.getProperty("halfExtentZ", 0.5));
            collider.radius = static_cast<float>(colliderNode.getProperty("radius", 0.5));
            collider.halfHeight = static_cast<float>(colliderNode.getProperty("halfHeight", 0.5));
            collider.collisionLayer = static_cast<std::uint16_t>(static_cast<int>(colliderNode.getProperty("collisionLayer", 0)));
            collider.isSensor = static_cast<bool>(colliderNode.getProperty("isSensor", false));
            reg.emplace<physics::ColliderComponent>(entity, collider);
        }

        if (const auto behaviorsNode = entityNode.getChildWithName("Behaviors"); behaviorsNode.isValid())
        {
            BehaviorAttachments behaviors;
            for (const auto behaviorNode : behaviorsNode)
                if (behaviorNode.hasType("Behavior"))
                    behaviors.podIds.push_back(behaviorNode.getProperty("podId").toString());
            reg.emplace<BehaviorAttachments>(entity, std::move(behaviors));
        }

        if (const auto objectStateNode = entityNode.getChildWithName("ObjectState"); objectStateNode.isValid())
        {
            ObjectState objectState;
            for (int index = 0; index < objectStateNode.getNumProperties(); ++index)
            {
                const auto propertyName = objectStateNode.getPropertyName(index);
                objectState.values.set(propertyName, objectStateNode.getProperty(propertyName));
            }
            reg.emplace<ObjectState>(entity, std::move(objectState));
        }

        // Only reconstruct a private Material here when there's no catalog
        // asset to resolve one from (meshAssetId empty -- a procedural/
        // authored-in-place entity, the same case AssetPlacement.cpp never
        // handles). When meshAssetId IS present, ViewportComponent's
        // renderOpenGL() resolve loop creates this entity's MeshRenderer
        // from AssetCatalog::Find() instead, sharing the SAME Material
        // instance every other entity using that asset shares -- matching
        // MaterialsPanel's own documented design (editing a shared
        // Material is expected to affect every entity sharing it, exactly
        // like Mesh already does). Building a private clone here for
        // EVERY entity regardless of asset identity was the actual bug:
        // it silently detached every reloaded entity from the catalog's
        // live Material, so neither PBR edits nor a compiled node-graph
        // material (MaterialGraphPanel) ever showed up again after a
        // save/reload round trip.
        auto mNode = entityNode.getChildWithName("Material");
        if (mNode.isValid() && meshAssetId.isEmpty())
        {
            auto mat = std::make_shared<Material>();
            mat->albedo.x = static_cast<float>(mNode.getProperty("albedoR", 0.7));
            mat->albedo.y = static_cast<float>(mNode.getProperty("albedoG", 0.15));
            mat->albedo.z = static_cast<float>(mNode.getProperty("albedoB", 0.15));
            mat->metallic = static_cast<float>(mNode.getProperty("metallic", 0.2));
            mat->roughness = static_cast<float>(mNode.getProperty("roughness", 0.4));

            MeshRenderer mr;
            mr.material = mat;
            reg.emplace<MeshRenderer>(entity, mr);
        }

    }

    for (const auto entityNode : entitiesNode)
    {
        if (! entityNode.hasType("Entity"))
            continue;

        int64_t savedId = entityNode.getProperty("id", -1);
        int64_t savedParentId = entityNode.getProperty("parentId", -1);

        if (savedId >= 0 && savedParentId >= 0 && idMap.count(savedId) > 0 && idMap.count(savedParentId) > 0)
        {
            auto entity = idMap[savedId];
            auto parentEntity = idMap[savedParentId];
            reg.emplace<Parent>(entity, parentEntity);
        }
    }

    return true;
}
}
