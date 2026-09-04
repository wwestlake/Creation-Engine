#pragma once

#include <juce_core/juce_core.h>

#include <creation/assets/ProjectSession.h>

#include "engine/world.h"

namespace ce::project
{
struct SceneDocumentInfo
{
    juce::String id;
    juce::String name;

    // The creation::assets::AssetKind::scene manifest entry this scene is
    // registered as (see EngineGameDocumentStore::saveScene) -- what makes
    // it a real, listable/openable/deletable Thing in Content Browser, not
    // just an id/name pair. saveGeneratedAsset mints its own id on first
    // save, which is not guaranteed to equal `id` above, so it's captured
    // here rather than assumed. Empty until the scene has been saved at
    // least once.
    juce::String catalogAssetId;
};

// A game names the exact installed content pack versions it is allowed to
// resolve. This is the dependency boundary shared by editor, runtime, and
// eventual export.
struct RequiredPackInfo
{
    juce::String packId;
    juce::String version;
};

struct GameDocumentInfo
{
    juce::String id;
    juce::String name;
    juce::String entrySceneId;
    juce::Array<SceneDocumentInfo> scenes;
    juce::Array<RequiredPackInfo> requiredPacks;

    // The creation::assets::AssetKind::game manifest entry this game is
    // registered as -- same reasoning as SceneDocumentInfo::catalogAssetId
    // above. games.xml (via saveGames) stays the sole source of truth for
    // scenes/entrySceneId/requiredPacks; this id exists only so Content
    // Browser can list/open/delete the game itself.
    juce::String catalogAssetId;

    [[nodiscard]] juce::String scenePath(const juce::String& sceneId) const;
    [[nodiscard]] juce::String assetRoot() const;
    [[nodiscard]] juce::String codeRoot() const;
};

// The only persistence boundary between an Engine game and its Suite project.
// All locations are VFS logical paths; a game never learns a machine path.
class EngineGameDocumentStore final
{
public:
    static constexpr const char* catalogPath = "engine/games.xml";

    static bool loadGames(creation::assets::ProjectSession& session,
                          juce::Array<GameDocumentInfo>& games,
                          juce::String& errorMessage);
    static bool ensureInitialGame(creation::assets::ProjectSession& session,
                                  juce::Array<GameDocumentInfo>& games,
                                  juce::String& errorMessage);
    static bool createGame(creation::assets::ProjectSession& session,
                           const juce::String& name,
                           GameDocumentInfo& createdGame,
                           SceneDocumentInfo& createdScene,
                           juce::String& errorMessage,
                           const juce::String& templateSceneId = "DefaultScene");
    static bool createScene(creation::assets::ProjectSession& session,
                            GameDocumentInfo& game,
                            const juce::String& name,
                            SceneDocumentInfo& createdScene,
                            juce::String& errorMessage,
                            const juce::String& templateSceneId = "DefaultScene");

    // Removes a scene from `game` (in-memory and its registered asset),
    // reassigning entrySceneId if the deleted scene was the entry point.
    // Rejects deleting a game's last remaining scene -- a Game must always
    // have at least one Scene (see docs/OBJECT_MODEL.md).
    static bool deleteScene(creation::assets::ProjectSession& session,
                            GameDocumentInfo& game,
                            const juce::String& sceneId,
                            juce::String& errorMessage);

    // Removes a game (cascading deleteScene over every one of its scenes
    // first, then its own registered asset) from `games`. Rejects deleting
    // a project's last remaining game -- openActiveGame already hard-
    // assumes at least one exists.
    static bool deleteGame(creation::assets::ProjectSession& session,
                           juce::Array<GameDocumentInfo>& games,
                           const juce::String& gameId,
                           juce::String& errorMessage);

    static bool renameGame(creation::assets::ProjectSession& session,
                           juce::Array<GameDocumentInfo>& games,
                           const juce::String& gameId,
                           const juce::String& newName,
                           juce::String& errorMessage);

    static bool renameScene(creation::assets::ProjectSession& session,
                            GameDocumentInfo& game,
                            const juce::String& sceneId,
                            const juce::String& newName,
                            juce::String& errorMessage);
    static bool saveScene(creation::assets::ProjectSession& session,
                          const GameDocumentInfo& game,
                          const SceneDocumentInfo& scene,
                          ce::engine::World& world,
                          juce::String& errorMessage);
    static bool loadScene(creation::assets::ProjectSession& session,
                          const GameDocumentInfo& game,
                          const SceneDocumentInfo& scene,
                          ce::engine::World& world,
                          juce::String& errorMessage);
    static bool saveGames(creation::assets::ProjectSession& session,
                          const juce::Array<GameDocumentInfo>& games,
                          juce::String& errorMessage);
};
} // namespace ce::project
