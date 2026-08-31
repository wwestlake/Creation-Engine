#pragma once

#include <functional>

#include <JuceHeader.h>

#include "Project/EngineGameDocument.h"

namespace ce::views
{
// Shared authoring state for the desktop dock today and a future spatial VR
// panel. It exposes game/scene selection and commands, never VFS details.
class GameScenePanel final : public juce::Component
{
public:
    GameScenePanel();

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
    void rebuildScenes();

    juce::Array<project::GameDocumentInfo> games_;
    juce::ComboBox gameSelector_;
    juce::ComboBox sceneSelector_;
    juce::TextButton newGameButton_ { "New Game" };
    juce::TextButton newSceneButton_ { "New Scene" };
    juce::TextButton saveButton_ { "Save" };
    juce::Label status_;
    bool synchronizing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameScenePanel)
};
} // namespace ce::views
