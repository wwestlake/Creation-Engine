#include "Project/EngineGameDocument.h"

#include "Scene/EngineSceneSerializer.h"
#include "Scene/Components.h"

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
    return game;
}

void CreateStandardStarterScene(ce::engine::World& world)
{
    std::lock_guard<std::mutex> lock(world.RegistryMutex());
    auto addBox = [&](const char* name, engine::Vec3 position, engine::Vec3 scale) {
        const auto entity = world.CreateEntity();
        scene::Transform transform;
        transform.position = position;
        transform.scale = scale;
        auto& registry = world.Registry();
        registry.emplace<scene::Name>(entity, scene::Name{ name });
        registry.emplace<scene::Transform>(entity, transform);
        registry.emplace<scene::MeshAssetReference>(entity, scene::MeshAssetReference{ "Cube" });
        registry.emplace<scene::SceneFlags>(entity, scene::SceneFlags{});
    };
    addBox("Center Block", { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f });
    addBox("Floor", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.1f, 10.0f });
    addBox("North Wall", { 0.0f, 2.0f, -10.0f }, { 10.0f, 2.5f, 0.1f });
    addBox("South Wall", { 0.0f, 2.0f, 10.0f }, { 10.0f, 2.5f, 0.1f });
    addBox("West Wall", { -10.0f, 2.0f, 0.0f }, { 0.1f, 2.5f, 10.0f });
    addBox("East Wall", { 10.0f, 2.0f, 0.0f }, { 0.1f, 2.5f, 10.0f });
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
    SceneDocumentInfo scene{ NewId(), "Main" };
    game.entrySceneId = scene.id;
    game.scenes.add(scene);

    // Greenfield Engine projects start with explicit documents. There is no
    // compatibility path for the obsolete unnamed-session format.
    ce::engine::World starterWorld;
    CreateStandardStarterScene(starterWorld);
    if (!saveScene(session, game, scene, starterWorld, errorMessage)) return false;
    games.add(game);
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::createGame(creation::assets::ProjectSession& session,
                                         const juce::String& name,
                                         ce::engine::World& initialWorld,
                                         GameDocumentInfo& createdGame,
                                         SceneDocumentInfo& createdScene,
                                         juce::String& errorMessage)
{
    juce::Array<GameDocumentInfo> games;
    if (!ensureInitialGame(session, games, errorMessage)) return false;
    createdGame.id = NewId();
    createdGame.name = name.trim().isEmpty() ? "New Game" : name.trim();
    createdScene = { NewId(), "Main" };
    createdGame.entrySceneId = createdScene.id;
    createdGame.scenes.add(createdScene);
    if (!saveScene(session, createdGame, createdScene, initialWorld, errorMessage)) return false;
    games.add(createdGame);
    return saveGames(session, games, errorMessage);
}

bool EngineGameDocumentStore::createScene(creation::assets::ProjectSession& session,
                                          GameDocumentInfo& game,
                                          const juce::String& name,
                                          ce::engine::World& initialWorld,
                                          SceneDocumentInfo& createdScene,
                                          juce::String& errorMessage)
{
    createdScene = { NewId(), name.trim().isEmpty() ? "New Scene" : name.trim() };
    if (!saveScene(session, game, createdScene, initialWorld, errorMessage)) return false;
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
