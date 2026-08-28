#include "Scene/EngineSceneSerializer.h"

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
        juce::ValueTree entityNode("Entity");
        entityNode.setProperty("id", static_cast<int64_t>(entity), nullptr);

        if (auto* name = reg.try_get<Name>(entity))
            entityNode.setProperty("name", name->value, nullptr);

        if (auto* parent = reg.try_get<Parent>(entity))
        {
            if (parent->value != entt::null)
                entityNode.setProperty("parentId", static_cast<int64_t>(parent->value), nullptr);
        }

        if (reg.all_of<Folder>(entity))
            entityNode.setProperty("isFolder", true, nullptr);

        if (auto* flags = reg.try_get<SceneFlags>(entity))
        {
            entityNode.setProperty("visible", flags->visible, nullptr);
            entityNode.setProperty("locked", flags->locked, nullptr);
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

        if (entityNode.getProperty("isFolder", false))
            reg.emplace<Folder>(entity);

        SceneFlags flags;
        flags.visible = entityNode.getProperty("visible", true);
        flags.locked = entityNode.getProperty("locked", false);
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

        auto mNode = entityNode.getChildWithName("Material");
        if (mNode.isValid())
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
