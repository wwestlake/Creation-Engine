#include "Views/GameScenePanel.h"

namespace ce::views
{
GameScenePanel::GameScenePanel()
{
    addAndMakeVisible(gameSelector_);
    addAndMakeVisible(sceneSelector_);
    addAndMakeVisible(newGameButton_);
    addAndMakeVisible(newSceneButton_);
    addAndMakeVisible(saveButton_);
    addAndMakeVisible(status_);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff9fb1c7));
    status_.setJustificationType(juce::Justification::centredLeft);
    gameSelector_.onChange = [this] {
        const auto index = gameSelector_.getSelectedItemIndex();
        if (!synchronizing_ && index >= 0 && index < games_.size() && onGameSelected)
            onGameSelected(games_.getReference(index).id);
    };
    sceneSelector_.onChange = [this] {
        const auto gameIndex = gameSelector_.getSelectedItemIndex();
        const auto sceneIndex = sceneSelector_.getSelectedItemIndex();
        if (!synchronizing_ && gameIndex >= 0 && gameIndex < games_.size() && sceneIndex >= 0 &&
            sceneIndex < games_.getReference(gameIndex).scenes.size() && onSceneSelected)
            onSceneSelected(games_.getReference(gameIndex).scenes.getReference(sceneIndex).id);
    };
    newGameButton_.onClick = [this] { if (onCreateGameRequested) onCreateGameRequested(); };
    newSceneButton_.onClick = [this] { if (onCreateSceneRequested) onCreateSceneRequested(); };
    saveButton_.onClick = [this] { if (onSaveRequested) onSaveRequested(); };
}

void GameScenePanel::setDocuments(const juce::Array<project::GameDocumentInfo>& games,
                                  const juce::String& activeGameId, const juce::String& activeSceneId)
{
    synchronizing_ = true;
    games_ = games;
    gameSelector_.clear(juce::dontSendNotification);
    int selected = 0;
    for (int index = 0; index < games_.size(); ++index) {
        gameSelector_.addItem(games_.getReference(index).name, index + 1);
        if (games_.getReference(index).id == activeGameId) selected = index + 1;
    }
    gameSelector_.setSelectedId(selected, juce::dontSendNotification);
    rebuildScenes();
    for (int index = 0; selected > 0 && index < games_.getReference(selected - 1).scenes.size(); ++index)
        if (games_.getReference(selected - 1).scenes.getReference(index).id == activeSceneId)
            sceneSelector_.setSelectedId(index + 1, juce::dontSendNotification);
    synchronizing_ = false;
}

void GameScenePanel::setStatus(const juce::String& status) { status_.setText(status, juce::dontSendNotification); }

void GameScenePanel::rebuildScenes()
{
    sceneSelector_.clear(juce::dontSendNotification);
    const auto selected = gameSelector_.getSelectedItemIndex();
    if (selected < 0 || selected >= games_.size()) return;
    const auto& game = games_.getReference(selected);
    for (int index = 0; index < game.scenes.size(); ++index)
        sceneSelector_.addItem(game.scenes.getReference(index).name, index + 1);
}

void GameScenePanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    gameSelector_.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);
    sceneSelector_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    auto buttons = area.removeFromTop(28);
    newGameButton_.setBounds(buttons.removeFromLeft(82));
    buttons.removeFromLeft(6);
    newSceneButton_.setBounds(buttons.removeFromLeft(86));
    buttons.removeFromLeft(6);
    saveButton_.setBounds(buttons.removeFromLeft(58));
    area.removeFromTop(8);
    status_.setBounds(area.removeFromTop(40));
}

void GameScenePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff151b24));
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    g.drawText("Game & Scene", 10, 4, getWidth() - 20, 20, juce::Justification::centredLeft);
}
} // namespace ce::views
