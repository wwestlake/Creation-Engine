#pragma once

#include <creation/assets/ProjectSession.h>

#include "Input/InputBindingTypes.h"
#include "Project/EngineGameDocument.h"

namespace ce::input
{
// One InputBindings document per Game, persisted the same way games.xml
// itself is (EngineGameDocumentStore::saveGames/loadGames) -- straight
// through ProjectSession::writeEntry/readEntry, with zero
// ProjectAssetService/AssetDescriptor involvement. games.xml is the
// existing, load-bearing precedent for "real per-Game data that is not a
// registered, separately-browsable asset" (unlike a Scene, which genuinely
// is one and goes through ProjectAssetService::saveGeneratedAsset) -- this
// follows that same shape, not the Scene shape.
class InputBindingDocumentStore final
{
public:
    [[nodiscard]] static juce::String path(const project::GameDocumentInfo& game);

    // A missing document is not a failure -- returns an empty
    // InputBindingSet, same "no entry yet is fine" shape
    // EngineGameDocumentStore::loadGames already uses for a brand-new
    // project.
    static bool load(creation::assets::ProjectSession& session,
                      const project::GameDocumentInfo& game,
                      InputBindingSet& result,
                      juce::String& errorMessage);

    // Does not call session.commit() -- same as saveScene/saveGames, the
    // caller commits once after whatever batch of writes it's making.
    static bool save(creation::assets::ProjectSession& session,
                      const project::GameDocumentInfo& game,
                      const InputBindingSet& bindings,
                      juce::String& errorMessage);
};
} // namespace ce::input
