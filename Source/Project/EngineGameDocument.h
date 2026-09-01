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
                           juce::String& errorMessage);
    static bool createScene(creation::assets::ProjectSession& session,
                            GameDocumentInfo& game,
                            const juce::String& name,
                            SceneDocumentInfo& createdScene,
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
