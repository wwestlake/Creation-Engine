#include "Project/EngineGameDocument.h"

#include "Assets/EngineAssetPack.h"
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
    return { node.getProperty("id").toString(), node.getProperty("name").toString() };
}

GameDocumentInfo ReadGameInfo(const juce::ValueTree& node)
{
    GameDocumentInfo game;
    game.id = node.getProperty("id").toString();
    game.name = node.getProperty("name").toString();
    game.entrySceneId = node.getProperty("entrySceneId").toString();
    for (const auto child : node)
        if (child.hasType("Scene")) game.scenes.add(ReadSceneInfo(child));
    if (const auto requiredPacks = node.getChildWithName("RequiredPacks"); requiredPacks.isValid())
        for (const auto packNode : requiredPacks)
            if (packNode.hasType("Pack"))
                game.requiredPacks.add({ packNode.getProperty("packId").toString(),
                                         packNode.getProperty("version").toString() });
    return game;
}

// A new game scene is a project-owned copy of the authored Default Scene in
// the Creation Engine Pack. C++ only reads and stores authored scene data.
bool CopyDefaultScene(creation::assets::ProjectSession& session,
                      const GameDocumentInfo& game,
                      const SceneDocumentInfo& scene,
                      juce::String& error)
{
    juce::MemoryBlock defaultSceneData;
    if (! ce::assets::EngineAssetPack::readDefaultScene(defaultSceneData, error)) return false;

    const auto xml = juce::XmlDocument::parse(juce::String::createStringFromData(
        defaultSceneData.getData(), static_cast<int>(defaultSceneData.getSize())));
    if (xml == nullptr)
    {
        error = "The Creation Engine Pack Default Scene is not valid scene data.";
        return false;
    }

    auto sceneTree = juce::ValueTree::fromXml(*xml);
    if (! sceneTree.hasType("CreationEngineScene"))
    {
        error = "The Creation Engine Pack Default Scene has the wrong document type.";
        return false;
    }

    juce::ValueTree document("CreationEngineSceneDocument");
    document.setProperty("gameId", game.id, nullptr);
    document.setProperty("sceneId", scene.id, nullptr);
    document.setProperty("sceneName", scene.name, nullptr);
    document.addChild(sceneTree, -1, nullptr);
    if (! session.writeEntry(game.scenePath(scene.id), ToMemory(document)))
    {
        error = "Could not create scene from the Creation Engine Pack Default Scene.";
        return false;
    }
    return true;
}

juce::ValueTree WriteGameInfo(const GameDocumentInfo& game)
{
    juce::ValueTree node("Game");
    node.setProperty("id", game.id, nullptr);
    node.setProperty("name", game.name, nullptr);
    node.setProperty("entrySceneId", game.entrySceneId, nullptr);
    for (const auto& scene : game.scenes) {
        juce::ValueTree sceneNode("Scene");
        sceneNode.setProperty("id", scene.id, nullptr);
        sceneNode.setProperty("name", scene.name, nullptr);
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
    game.scenes.add(scene);

    if (! CopyDefaultScene(session, game, scene, errorMessage)) return false;
    games.add(game);
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::createGame(creation::assets::ProjectSession& session,
                                         const juce::String& name,
                                         GameDocumentInfo& createdGame,
                                         SceneDocumentInfo& createdScene,
                                         juce::String& errorMessage)
{
    juce::Array<GameDocumentInfo> games;
    if (!ensureInitialGame(session, games, errorMessage)) return false;
    createdGame.id = NewId();
    createdGame.name = name.trim().isEmpty() ? "New Game" : name.trim();
    createdGame.requiredPacks.add({ ce::assets::EngineAssetPack::packId, ce::assets::EngineAssetPack::version });
    createdScene = { NewId(), "Main" };
    createdGame.entrySceneId = createdScene.id;
    createdGame.scenes.add(createdScene);
    if (! CopyDefaultScene(session, createdGame, createdScene, errorMessage)) return false;
    games.add(createdGame);
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::createScene(creation::assets::ProjectSession& session,
                                          GameDocumentInfo& game,
                                          const juce::String& name,
                                          SceneDocumentInfo& createdScene,
                                          juce::String& errorMessage)
{
    createdScene = { NewId(), name.trim().isEmpty() ? "New Scene" : name.trim() };
    if (! CopyDefaultScene(session, game, createdScene, errorMessage)) return false;
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
    if (!session.writeEntry(game.scenePath(scene.id), ToMemory(document))) {
        errorMessage = "Could not save scene: " + scene.name;
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
    world.ResetTick();
    return true;
}
} // namespace ce::project
