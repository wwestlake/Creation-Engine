#include "Project/EngineGameDocument.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/EngineAssetPack.h"
#include "Scene/Components.h"
#include "Scene/EngineSceneSerializer.h"

#include <iterator>

namespace ce::project
{
namespace
{
juce::String NewId()
{
    return juce::Uuid().toString().replaceCharacters("-", "");
}

juce::MemoryBlock ToMemory(const juce::ValueTree& tree)
{
    const auto xml = tree.createXml();
    const auto text = xml != nullptr ? xml->toString() : juce::String{};
    return { text.toRawUTF8(), static_cast<std::size_t>(text.getNumBytesAsUTF8()) };
}

bool ReadTree(creation::assets::ProjectSession& session, const juce::String& path,
              juce::ValueTree& result, juce::String& error)
{
    juce::MemoryBlock data;
    if (!session.readEntry(path, data)) {
        error = "Missing Engine document: " + path;
        return false;
    }
    const auto text = juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize()));
    const auto xml = juce::XmlDocument::parse(text);
    if (xml == nullptr) {
        error = "Invalid Engine document: " + path;
        return false;
    }
    result = juce::ValueTree::fromXml(*xml);
    return true;
}

SceneDocumentInfo ReadSceneInfo(const juce::ValueTree& node)
{
    return { node.getProperty("id").toString(), node.getProperty("name").toString(),
             node.getProperty("catalogAssetId").toString() };
}

GameDocumentInfo ReadGameInfo(const juce::ValueTree& node)
{
    GameDocumentInfo game;
    game.id = node.getProperty("id").toString();
    game.name = node.getProperty("name").toString();
    game.entrySceneId = node.getProperty("entrySceneId").toString();
    game.catalogAssetId = node.getProperty("catalogAssetId").toString();
    game.inputMappingPackId = node.getProperty("inputMappingPackId").toString();
    game.inputMappingPackVersion = node.getProperty("inputMappingPackVersion").toString();
    game.inputMappingEntryPath = node.getProperty("inputMappingEntryPath").toString();
    game.inputMappingAssetId = node.getProperty("inputMappingAssetId").toString();
    game.inputMappingAssetVersionId = node.getProperty("inputMappingAssetVersionId").toString();
    for (const auto child : node)
        if (child.hasType("Scene")) game.scenes.add(ReadSceneInfo(child));
    if (const auto requiredPacks = node.getChildWithName("RequiredPacks"); requiredPacks.isValid())
        for (const auto packNode : requiredPacks)
            if (packNode.hasType("Pack"))
                game.requiredPacks.add({ packNode.getProperty("packId").toString(),
                                         packNode.getProperty("version").toString() });
    if (const auto playerSlots = node.getChildWithName("PlayerSlots"); playerSlots.isValid())
        for (const auto slotNode : playerSlots)
            if (slotNode.hasType("PlayerSlot"))
                game.playerSlots.add({ slotNode.getProperty("id").toString(),
                                       slotNode.getProperty("displayName").toString(),
                                       slotNode.getProperty("creatorPolicyAssetId").toString(),
                                       slotNode.getProperty("creatorPolicyVersionId").toString(),
                                       slotNode.getProperty("defaultRosterAssetId").toString(),
                                       slotNode.getProperty("defaultInstanceAssetId").toString() });
    // Older game documents predate PlayerSlots.  They still participate in
    // the same suite-wide possession contract through the standard slot.
    if (game.playerSlots.isEmpty())
        game.playerSlots.add({ "player-1", "Player 1", {}, {}, {}, {} });
    return game;
}

// A new game scene is a project-owned copy of one authored starter scene
// from the Creation Engine Pack (e.g. "DefaultScene", or one of the
// starter-content templates the New Game picker offers). C++ only reads
// and stores authored scene data.
bool CopyStarterScene(creation::assets::ProjectSession& session,
                      const GameDocumentInfo& game,
                      SceneDocumentInfo& scene,
                      const juce::String& templateSceneId,
                      juce::String& error)
{
    juce::MemoryBlock templateSceneData;
    if (! ce::assets::EngineAssetPack::readScene(templateSceneId, templateSceneData, error)) return false;

    const auto xml = juce::XmlDocument::parse(juce::String::createStringFromData(
        templateSceneData.getData(), static_cast<int>(templateSceneData.getSize())));
    if (xml == nullptr)
    {
        error = "The Djehuti Engine Pack scene \"" + templateSceneId + "\" is not valid scene data.";
        return false;
    }

    auto sceneTree = juce::ValueTree::fromXml(*xml);
    if (! sceneTree.hasType("CreationEngineScene"))
    {
        error = "The Djehuti Engine Pack scene \"" + templateSceneId + "\" has the wrong document type.";
        return false;
    }

    juce::ValueTree document("CreationEngineSceneDocument");
    document.setProperty("gameId", game.id, nullptr);
    document.setProperty("sceneId", scene.id, nullptr);
    document.setProperty("sceneName", scene.name, nullptr);
    document.addChild(sceneTree, -1, nullptr);

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::scene;
    options.displayName = scene.name;
    options.logicalPath = game.scenePath(scene.id);
    options.mediaType = "application/x-creation-engine-scene";
    options.sourceApp = "Djehuti Engine";
    options.description = "Djehuti Engine Scene";
    creation::assets::AssetDescriptor savedAsset;
    if (! creation::assets::ProjectAssetService::saveGeneratedAsset(session, ToMemory(document), options, savedAsset, error))
    {
        error = "Could not create scene from \"" + templateSceneId + "\": " + error;
        return false;
    }
    scene.catalogAssetId = savedAsset.id;
    return true;
}

// Registers `game` as a real, listable/openable/deletable Thing (see
// docs/OBJECT_MODEL.md's "Entity, Thing, Object" section) -- the payload
// itself is a small inert marker, not a real data source; games.xml (via
// saveGames) stays the sole source of truth for scenes/entrySceneId/
// requiredPacks. This asset exists purely so Content Browser has
// something real to list/open/delete for "the game."
bool RegisterGameAsset(creation::assets::ProjectSession& session, GameDocumentInfo& game, juce::String& error)
{
    juce::ValueTree marker("GameRef");
    marker.setProperty("id", game.id, nullptr);
    marker.setProperty("name", game.name, nullptr);

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::game;
    options.displayName = game.name;
    options.logicalPath = "engine/games/" + game.id + "/game.asset.xml";
    options.mediaType = "application/x-creation-engine-game";
    options.sourceApp = "Djehuti Engine";
    options.description = "Djehuti Engine Game";
    creation::assets::AssetDescriptor savedAsset;
    if (! creation::assets::ProjectAssetService::saveGeneratedAsset(session, ToMemory(marker), options, savedAsset, error))
        return false;
    game.catalogAssetId = savedAsset.id;
    return true;
}

juce::ValueTree WriteGameInfo(const GameDocumentInfo& game)
{
    juce::ValueTree node("Game");
    node.setProperty("id", game.id, nullptr);
    node.setProperty("name", game.name, nullptr);
    node.setProperty("entrySceneId", game.entrySceneId, nullptr);
    node.setProperty("catalogAssetId", game.catalogAssetId, nullptr);
    node.setProperty("inputMappingPackId", game.inputMappingPackId, nullptr);
    node.setProperty("inputMappingPackVersion", game.inputMappingPackVersion, nullptr);
    node.setProperty("inputMappingEntryPath", game.inputMappingEntryPath, nullptr);
    node.setProperty("inputMappingAssetId", game.inputMappingAssetId, nullptr);
    node.setProperty("inputMappingAssetVersionId", game.inputMappingAssetVersionId, nullptr);
    for (const auto& scene : game.scenes) {
        juce::ValueTree sceneNode("Scene");
        sceneNode.setProperty("id", scene.id, nullptr);
        sceneNode.setProperty("name", scene.name, nullptr);
        sceneNode.setProperty("catalogAssetId", scene.catalogAssetId, nullptr);
        node.addChild(sceneNode, -1, nullptr);
    }
    juce::ValueTree requiredPacks("RequiredPacks");
    for (const auto& pack : game.requiredPacks)
    {
        juce::ValueTree packNode("Pack");
        packNode.setProperty("packId", pack.packId, nullptr);
        packNode.setProperty("version", pack.version, nullptr);
        requiredPacks.addChild(packNode, -1, nullptr);
    }
    node.addChild(requiredPacks, -1, nullptr);
    juce::ValueTree playerSlots("PlayerSlots");
    for (const auto& slot : game.playerSlots)
    {
        juce::ValueTree slotNode("PlayerSlot");
        slotNode.setProperty("id", slot.id, nullptr);
        slotNode.setProperty("displayName", slot.displayName, nullptr);
        slotNode.setProperty("creatorPolicyAssetId", slot.creatorPolicyAssetId, nullptr);
        slotNode.setProperty("creatorPolicyVersionId", slot.creatorPolicyVersionId, nullptr);
        slotNode.setProperty("defaultRosterAssetId", slot.defaultRosterAssetId, nullptr);
        slotNode.setProperty("defaultInstanceAssetId", slot.defaultInstanceAssetId, nullptr);
        playerSlots.addChild(slotNode, -1, nullptr);
    }
    node.addChild(playerSlots, -1, nullptr);
    return node;
}

void AssignPackagedInputMapping(GameDocumentInfo& game)
{
    game.inputMappingPackId = ce::assets::EngineAssetPack::packId;
    game.inputMappingPackVersion = ce::assets::EngineAssetPack::version;
    game.inputMappingEntryPath = ce::assets::EngineAssetPack::defaultInputMappingPath();
    game.inputMappingAssetId = {};
    game.inputMappingAssetVersionId = {};
}

// Version 1.0.8's DefaultScene rendered eleven visible starter objects but
// authored no physics components for them. This intentionally narrow data
// migration recognizes only that complete, untouched layout; it never turns
// arbitrary mesh assets or partially configured designer scenes into bodies.
bool UpgradeUntouchedLegacyDefaultScenePhysics(juce::ValueTree sceneTree)
{
    struct StaticColliderSpec
    {
        const char* name;
        const char* meshAssetId;
        int shape;
        float halfExtentX;
        float halfExtentY;
        float halfExtentZ;
        float radius;
    };

    static constexpr StaticColliderSpec specs[] {
        { "Floor",        "Cube",   0, 15.0f, 0.05f, 15.0f, 0.0f },
        { "North Wall",   "Cube",   0, 15.0f, 1.25f, 0.05f, 0.0f },
        { "South Wall",   "Cube",   0, 15.0f, 1.25f, 0.05f, 0.0f },
        { "West Wall",    "Cube",   0, 0.05f, 1.25f, 15.0f, 0.0f },
        { "East Wall",    "Cube",   0, 0.05f, 1.25f, 15.0f, 0.0f },
        { "Center Block", "Cube",   0, 0.50f, 0.50f, 0.50f, 0.0f },
        { "Blue Sphere",  "Sphere", 1, 0.00f, 0.00f, 0.00f, 0.75f },
        { "Gold Sphere",  "Sphere", 1, 0.00f, 0.00f, 0.00f, 1.00f },
        { "Green Block",  "Cube",   0, 0.80f, 0.80f, 0.80f, 0.0f },
        { "Purple Block", "Cube",   0, 1.20f, 1.20f, 1.20f, 0.0f },
        { "Orange Block", "Cube",   0, 0.60f, 0.60f, 0.60f, 0.0f },
    };

    const auto entities = sceneTree.getChildWithName("Entities");
    if (!entities.isValid() || entities.getNumChildren() != static_cast<int>(std::size(specs)))
        return false;

    for (int index = 0; index < entities.getNumChildren(); ++index)
    {
        const auto entity = entities.getChild(index);
        const auto& spec = specs[index];
        if (!entity.hasType("Entity") || entity.getProperty("name").toString() != spec.name ||
            entity.getProperty("meshAssetId").toString() != spec.meshAssetId ||
            entity.getChildWithName("RigidBody").isValid() || entity.getChildWithName("Collider").isValid())
            return false;
    }

    for (int index = 0; index < entities.getNumChildren(); ++index)
    {
        auto entity = entities.getChild(index);
        const auto& spec = specs[index];
        juce::ValueTree rigidBody("RigidBody");
        rigidBody.setProperty("motionType", 0, nullptr); // Static
        entity.addChild(rigidBody, -1, nullptr);

        juce::ValueTree collider("Collider");
        collider.setProperty("shape", spec.shape, nullptr);
        collider.setProperty("halfExtentX", spec.halfExtentX, nullptr);
        collider.setProperty("halfExtentY", spec.halfExtentY, nullptr);
        collider.setProperty("halfExtentZ", spec.halfExtentZ, nullptr);
        collider.setProperty("radius", spec.radius, nullptr);
        collider.setProperty("halfHeight", 0.5f, nullptr);
        collider.setProperty("collisionLayer", 0, nullptr);
        collider.setProperty("isSensor", false, nullptr);
        entity.addChild(collider, -1, nullptr);
    }
    return true;
}
} // namespace

juce::String GameDocumentInfo::scenePath(const juce::String& sceneId) const
{
    return "engine/games/" + id + "/scenes/" + sceneId + ".xml";
}

juce::String GameDocumentInfo::assetRoot() const { return "engine/games/" + id + "/assets/"; }
juce::String GameDocumentInfo::codeRoot() const { return "engine/games/" + id + "/code/"; }

bool EngineGameDocumentStore::loadGames(creation::assets::ProjectSession& session,
                                        juce::Array<GameDocumentInfo>& games,
                                        juce::String& errorMessage)
{
    games.clear();
    if (!session.containsEntry(catalogPath)) return true;
    juce::ValueTree catalog;
    if (!ReadTree(session, catalogPath, catalog, errorMessage)) return false;
    if (!catalog.hasType("CreationEngineGames")) {
        errorMessage = "Engine game catalog has the wrong document type.";
        return false;
    }
    for (const auto child : catalog)
        if (child.hasType("Game")) games.add(ReadGameInfo(child));
    return true;
}

bool EngineGameDocumentStore::saveGames(creation::assets::ProjectSession& session,
                                        const juce::Array<GameDocumentInfo>& games,
                                        juce::String& errorMessage)
{
    juce::ValueTree catalog("CreationEngineGames");
    catalog.setProperty("formatVersion", 1, nullptr);
    for (const auto& game : games) catalog.addChild(WriteGameInfo(game), -1, nullptr);
    if (!session.writeEntry(catalogPath, ToMemory(catalog))) {
        errorMessage = "Could not save the Engine game catalog.";
        return false;
    }
    return true;
}

bool EngineGameDocumentStore::ensureInitialGame(creation::assets::ProjectSession& session,
                                                juce::Array<GameDocumentInfo>& games,
                                                juce::String& errorMessage)
{
    if (!loadGames(session, games, errorMessage)) return false;
    if (!games.isEmpty()) {
        bool migrated = false;
        for (auto& existing : games) {
            if (existing.inputMappingPackId.isEmpty() && existing.inputMappingAssetId.isEmpty()) {
                AssignPackagedInputMapping(existing);
                migrated = true;
            }
        }
        return !migrated || saveGames(session, games, errorMessage);
    }

    GameDocumentInfo game;
    game.id = NewId();
    game.name = "Game";
    game.requiredPacks.add({ ce::assets::EngineAssetPack::packId, ce::assets::EngineAssetPack::version });
    AssignPackagedInputMapping(game);
    game.playerSlots.add({ "player-1", "Player 1", {}, {}, {}, {} });
    SceneDocumentInfo scene{ NewId(), "Main" };
    game.entrySceneId = scene.id;

    if (! CopyStarterScene(session, game, scene, "DefaultScene", errorMessage)) return false;
    game.scenes.add(scene);
    if (! RegisterGameAsset(session, game, errorMessage)) return false;
    games.add(game);
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::createGame(creation::assets::ProjectSession& session,
                                         const juce::String& name,
                                         GameDocumentInfo& createdGame,
                                         SceneDocumentInfo& createdScene,
                                         juce::String& errorMessage,
                                         const juce::String& templateSceneId)
{
    juce::Array<GameDocumentInfo> games;
    if (!ensureInitialGame(session, games, errorMessage)) return false;
    createdGame.id = NewId();
    createdGame.name = name.trim().isEmpty() ? "New Game" : name.trim();
    createdGame.requiredPacks.add({ ce::assets::EngineAssetPack::packId, ce::assets::EngineAssetPack::version });
    AssignPackagedInputMapping(createdGame);
    createdGame.playerSlots.add({ "player-1", "Player 1", {}, {}, {}, {} });
    createdScene = { NewId(), "Main" };
    createdGame.entrySceneId = createdScene.id;
    if (! CopyStarterScene(session, createdGame, createdScene, templateSceneId, errorMessage)) return false;
    createdGame.scenes.add(createdScene);
    if (! RegisterGameAsset(session, createdGame, errorMessage)) return false;
    games.add(createdGame);
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::createScene(creation::assets::ProjectSession& session,
                                          GameDocumentInfo& game,
                                          const juce::String& name,
                                          SceneDocumentInfo& createdScene,
                                          juce::String& errorMessage,
                                          const juce::String& templateSceneId)
{
    createdScene = { NewId(), name.trim().isEmpty() ? "New Scene" : name.trim() };
    if (! CopyStarterScene(session, game, createdScene, templateSceneId, errorMessage)) return false;
    game.scenes.add(createdScene);
    juce::Array<GameDocumentInfo> games;
    if (!loadGames(session, games, errorMessage)) return false;
    for (auto& listedGame : games)
        if (listedGame.id == game.id) listedGame = game;
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::saveScene(creation::assets::ProjectSession& session,
                                        const GameDocumentInfo& game,
                                        const SceneDocumentInfo& scene,
                                        ce::engine::World& world,
                                        juce::String& errorMessage)
{
    juce::ValueTree document("CreationEngineSceneDocument");
    document.setProperty("gameId", game.id, nullptr);
    document.setProperty("sceneId", scene.id, nullptr);
    document.setProperty("sceneName", scene.name, nullptr);
    document.addChild(ce::scene::EngineSceneSerializer::serializeScene(world), -1, nullptr);

    // Same logicalPath every save -- saveGeneratedAsset reuses the existing
    // asset id and bumps its revision, so this is real version history,
    // not a rename/duplicate. scene.catalogAssetId (set once, at creation)
    // stays valid across every subsequent save.
    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::scene;
    options.displayName = scene.name;
    options.logicalPath = game.scenePath(scene.id);
    options.mediaType = "application/x-creation-engine-scene";
    options.sourceApp = "Djehuti Engine";
    options.description = "Djehuti Engine Scene";
    creation::assets::AssetDescriptor savedAsset;
    if (!creation::assets::ProjectAssetService::saveGeneratedAsset(session, ToMemory(document), options, savedAsset, errorMessage)) {
        errorMessage = "Could not save scene \"" + scene.name + "\": " + errorMessage;
        return false;
    }
    return true;
}

bool EngineGameDocumentStore::loadScene(creation::assets::ProjectSession& session,
                                        const GameDocumentInfo& game,
                                        const SceneDocumentInfo& scene,
                                        ce::engine::World& world,
                                        juce::String& errorMessage)
{
    juce::ValueTree document;
    if (!ReadTree(session, game.scenePath(scene.id), document, errorMessage)) return false;
    auto sceneState = document.hasType("CreationEngineSceneDocument")
        ? document.getChildWithName("CreationEngineScene") : document;
    const bool upgradedLegacyDefaultScene = UpgradeUntouchedLegacyDefaultScenePhysics(sceneState);
    if (!ce::scene::EngineSceneSerializer::restoreScene(world, sceneState)) {
        errorMessage = "Scene data could not be restored: " + scene.name;
        return false;
    }

    // The Scene itself is a real Thing with its own metric (see
    // docs/OBJECT_MODEL.md's "Entity, Thing, Object" section) -- a root
    // entity every otherwise-unparented entity is positioned relative to,
    // synthesized fresh here rather than persisted (see SceneRoot's own
    // comment). restoreScene() just rebuilt the registry from scratch, so
    // every entity restored above is either genuinely parented already or
    // needs attaching to this new root.
    {
        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        auto& reg = world.Registry();
        const auto root = reg.create();
        reg.emplace<ce::scene::Name>(root, ce::scene::Name{ scene.name });
        reg.emplace<ce::scene::Transform>(root, ce::scene::Transform{});
        reg.emplace<ce::scene::SceneRoot>(root, ce::scene::SceneRoot{ scene.id });

        for (auto entity : reg.storage<entt::entity>())
        {
            if (entity == root || reg.all_of<ce::scene::SceneRoot>(entity)) continue;
            // "Top-level" is Parent{entt::null} (the convention every
            // existing placement call site -- ObjectFactory::instantiate
            // included -- already uses for "no real parent"), not merely
            // "no Parent component at all". A restored entity with no
            // saved parentId falls into the same bucket, since
            // EngineSceneSerializer::restoreScene never emplaces Parent
            // for one at all -- try_get below handles both shapes.
            if (const auto* parent = reg.try_get<ce::scene::Parent>(entity); parent == nullptr || parent->value == entt::null)
                reg.emplace_or_replace<ce::scene::Parent>(entity, ce::scene::Parent{ root });
        }
    }

    world.ResetTick();
    if (upgradedLegacyDefaultScene && !saveScene(session, game, scene, world, errorMessage))
    {
        errorMessage = "Could not persist the Default Scene collision upgrade: " + errorMessage;
        return false;
    }
    return true;
}

namespace {
// Removes every version of a registered asset -- same shape
// ContentBrowserPanel::PerformDelete already uses generically for any
// AssetKind (Source/Views/ContentBrowserPanel.cpp). "Delete" here means
// gone, not "gone except for reimport history nobody can reach anymore."
bool RemoveRegisteredAsset(creation::assets::ProjectSession& session, const juce::String& assetId, juce::String& error)
{
    if (assetId.isEmpty()) return true; // never registered (e.g. created before this feature existed) -- nothing to remove.
    bool anyFailure = false;
    for (const auto& version : session.getManifest().assetCatalog.findAllVersions(assetId)) {
        if (!session.removeEntry(version.logicalPath)) anyFailure = true;
        if (!session.removeAssetDescriptorByVersionId(version.versionId)) anyFailure = true;
    }
    if (anyFailure) error = "Could not fully remove asset \"" + assetId + "\".";
    return !anyFailure;
}
} // namespace

bool EngineGameDocumentStore::deleteScene(creation::assets::ProjectSession& session,
                                          GameDocumentInfo& game,
                                          const juce::String& sceneId,
                                          juce::String& errorMessage)
{
    if (game.scenes.size() <= 1) {
        errorMessage = "A Game must always have at least one Scene.";
        return false;
    }
    const int index = [&] {
        for (int i = 0; i < game.scenes.size(); ++i)
            if (game.scenes.getReference(i).id == sceneId) return i;
        return -1;
    }();
    if (index < 0) {
        errorMessage = "Scene not found.";
        return false;
    }

    const auto removedScene = game.scenes[index];
    RemoveRegisteredAsset(session, removedScene.catalogAssetId, errorMessage);
    session.removeEntry(game.scenePath(sceneId));
    game.scenes.remove(index);
    if (game.entrySceneId == sceneId) game.entrySceneId = game.scenes.getReference(0).id;

    juce::Array<GameDocumentInfo> games;
    if (!loadGames(session, games, errorMessage)) return false;
    for (auto& listedGame : games)
        if (listedGame.id == game.id) listedGame = game;
    if (!saveGames(session, games, errorMessage)) return false;
    return session.commit(errorMessage);
}

bool EngineGameDocumentStore::deleteGame(creation::assets::ProjectSession& session,
                                         juce::Array<GameDocumentInfo>& games,
                                         const juce::String& gameId,
                                         juce::String& errorMessage)
{
    if (games.size() <= 1) {
        errorMessage = "A project must always have at least one Game.";
        return false;
    }
    const int index = [&] {
        for (int i = 0; i < games.size(); ++i)
            if (games.getReference(i).id == gameId) return i;
        return -1;
    }();
    if (index < 0) {
        errorMessage = "Game not found.";
        return false;
    }

    auto game = games[index];
    for (const auto& scene : game.scenes) {
        RemoveRegisteredAsset(session, scene.catalogAssetId, errorMessage);
        session.removeEntry(game.scenePath(scene.id));
    }
    RemoveRegisteredAsset(session, game.catalogAssetId, errorMessage);
    games.remove(index);

    if (!saveGames(session, games, errorMessage)) return false;
    return session.commit(errorMessage);
}

bool EngineGameDocumentStore::renameGame(creation::assets::ProjectSession& session,
                                         juce::Array<GameDocumentInfo>& games,
                                         const juce::String& gameId,
                                         const juce::String& newName,
                                         juce::String& errorMessage)
{
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty()) {
        errorMessage = "A Game needs a name.";
        return false;
    }
    for (auto& game : games) {
        if (game.id != gameId) continue;
        game.name = trimmed;
        if (!RegisterGameAsset(session, game, errorMessage)) return false; // same logicalPath -- bumps revision, updates displayName.
        if (!saveGames(session, games, errorMessage)) return false;
        return session.commit(errorMessage);
    }
    errorMessage = "Game not found.";
    return false;
}

bool EngineGameDocumentStore::renameScene(creation::assets::ProjectSession& session,
                                          GameDocumentInfo& game,
                                          const juce::String& sceneId,
                                          const juce::String& newName,
                                          juce::String& errorMessage)
{
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty()) {
        errorMessage = "A Scene needs a name.";
        return false;
    }
    for (auto& scene : game.scenes) {
        if (scene.id != sceneId) continue;
        scene.name = trimmed;

        juce::ValueTree document;
        if (!ReadTree(session, game.scenePath(sceneId), document, errorMessage)) return false;
        document.setProperty("sceneName", trimmed, nullptr);

        creation::assets::ProjectAssetService::ImportOptions options;
        options.kind = creation::assets::AssetKind::scene;
        options.displayName = trimmed;
        options.logicalPath = game.scenePath(sceneId);
        options.mediaType = "application/x-creation-engine-scene";
        options.sourceApp = "Djehuti Engine";
        options.description = "Djehuti Engine Scene";
        creation::assets::AssetDescriptor savedAsset;
        if (!creation::assets::ProjectAssetService::saveGeneratedAsset(session, ToMemory(document), options, savedAsset, errorMessage))
            return false;

        juce::Array<GameDocumentInfo> games;
        if (!loadGames(session, games, errorMessage)) return false;
        for (auto& listedGame : games)
            if (listedGame.id == game.id) listedGame = game;
        if (!saveGames(session, games, errorMessage)) return false;
        return session.commit(errorMessage);
    }
    errorMessage = "Scene not found.";
    return false;
}
} // namespace ce::project
