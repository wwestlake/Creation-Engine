#pragma once

#include <creation/assets/ProjectSession.h>

#include "Input/InputBindingTypes.h"
#include "Project/EngineGameDocument.h"

namespace ce::input
{
// A Game references an input-mapping JSON asset. The initial reference points
// to immutable Engine Pack content; designer edits publish a project-owned
// mapping asset and retarget the Game without changing the packaged original.
class InputBindingDocumentStore final
{
public:
    static bool load(const creation::assets::ProjectSession& session,
                      const project::GameDocumentInfo& game,
                      const juce::String& contextId,
                      InputBindingSet& result,
                      juce::String& errorMessage);

    static bool save(creation::assets::ProjectSession& session,
                      project::GameDocumentInfo& game,
                      const juce::String& contextId,
                      const InputBindingSet& bindings,
                      juce::String& errorMessage);
};
} // namespace ce::input
