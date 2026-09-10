#include "Views/SceneBuiltinsPanel.h"

namespace ce::views
{
SceneBuiltinsPanel::SceneBuiltinsPanel()
{
    heading_.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    heading_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(heading_);
    help_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(help_);

    addTile(playerStart_, scene::BuiltInKind::playerStart,
            "Creates the Player 1 possession start at the current viewport placement point.");
    addTile(spawner_, scene::BuiltInKind::spawner,
            "Creates a configurable generic spawner.");
    addTile(cameraMarker_, scene::BuiltInKind::cameraMarker,
            "Creates an authored camera marker.");
    addTile(triggerVolume_, scene::BuiltInKind::triggerVolume,
            "Creates an authored trigger-volume marker.");
    addTile(waypoint_, scene::BuiltInKind::waypoint,
            "Creates an authored waypoint marker.");
    addTile(audioEmitter_, scene::BuiltInKind::audioEmitter,
            "Creates an authored audio-emitter marker.");
}

void SceneBuiltinsPanel::addTile(juce::TextButton& button, scene::BuiltInKind kind, const juce::String& tooltip)
{
    button.setTooltip(tooltip);
    button.onClick = [this, kind] { if (onCreateBuiltIn) onCreateBuiltIn(kind); };
    addAndMakeVisible(button);
}

void SceneBuiltinsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15181d));
}

void SceneBuiltinsPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    heading_.setBounds(area.removeFromTop(24));
    help_.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    const int rowHeight = 34;
    playerStart_.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(4);
    spawner_.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(4);
    cameraMarker_.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(4);
    triggerVolume_.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(4);
    waypoint_.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(4);
    audioEmitter_.setBounds(area.removeFromTop(rowHeight));
}
} // namespace ce::views
