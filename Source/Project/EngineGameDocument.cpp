#include "Project/EngineGameDocument.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/EngineAssetPack.h"
#include "Scene/Components.h"
#include "Scene/EngineSceneSerializer.h"

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
    for (const auto child : node)
        if (child.hasType("Scene")) game.scenes.add(ReadSceneInfo(child));
    if (const auto requiredPacks = node.getChildWithName("RequiredPacks"); requiredPacks.isValid())
        for (const auto packNode : requiredPacks)
            if (packNode.hasType("Pack"))
                game.requiredPacks.add({ packNode.getProperty("packId").toString(),
                                         packNode.getProperty("version").toString() });
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
        error = "The Creation Engine Pack scene \"" + templateSceneId + "\" is not valid scene data.";
        return false;
    }

    auto sceneTree = juce::ValueTree::fromXml(*xml);
    if (! sceneTree.hasType("CreationEngineScene"))
    {
        error = "The Creation Engine Pack scene \"" + templateSceneId + "\" has the wrong document type.";
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
    options.sourceApp = "Creation Engine";
    options.description = "Creation Engine Scene";
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
    options.sourceApp = "Creation Engine";
    options.description = "Creation Engine Game";
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
    return node;
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
    if (!games.isEmpty()) return true;

    GameDocumentInfo game;
    game.id = NewId();
    game.name = "Game";
    game.requiredPacks.add({ ce::assets::EngineAssetPack::packId, ce::assets::EngineAssetPack::version });
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
    options.sourceApp = "Creation Engine";
    options.description = "Creation Engine Scene";
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
    const auto sceneState = document.hasType("CreationEngineSceneDocument")
        ? document.getChildWithName("CreationEngineScene") : document;
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
        options.sourceApp = "Creation Engine";
        options.description = "Creation Engine Scene";
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
