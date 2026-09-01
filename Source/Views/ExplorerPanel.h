#pragma once

#include <functional>
#include <memory>

#include <JuceHeader.h>

#include "Project/EngineGameDocument.h"

namespace ce::views
{
// The engine-level resource explorer: a Games -> Scenes tree scoped to this
// app's own documents. Distinct from the suite-level Asset Manager (reached
// via the header bar's Project menu), which handles cross-app asset browsing
// -- this panel only knows about the game/scene structure Creation Engine
// itself authors. Replaces the old GameScenePanel (flat game/scene combo
// boxes); same callback surface, so it drops into MainComponent's existing
// wiring directly.
class ExplorerPanel final : public juce::Component
{
public:
    ExplorerPanel();
    ~ExplorerPanel() override;

    void setDocuments(const juce::Array<project::GameDocumentInfo>& games,
                      const juce::String& activeGameId, const juce::String& activeSceneId);
    void setStatus(const juce::String& status);
    void resized() override;
    void paint(juce::Graphics& g) override;

    std::function<void(const juce::String&)> onGameSelected;
    std::function<void(const juce::String&)> onSceneSelected;
    std::function<void()> onCreateGameRequested;
    std::function<void()> onCreateSceneRequested;
    std::function<void()> onSaveRequested;

private:
    class RootItem;
    class GameItem;
    class SceneItem;

    juce::Label titleLabel_{ {}, "Explorer" };
    juce::TreeView tree_;
    std::unique_ptr<RootItem> root_;
    juce::TextButton newGameButton_ { "New Game" };
    juce::TextButton newSceneButton_ { "New Scene" };
    juce::TextButton saveButton_ { "Save" };
    juce::Label status_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExplorerPanel)
};
} // namespace ce::views
