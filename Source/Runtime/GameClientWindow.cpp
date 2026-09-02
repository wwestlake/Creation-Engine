#include "GameClientWindow.h"

#include <creation/ui/CreationSuiteLogos.h>
#include "engine/foundation_gameplay.h"

#include "Scene/EngineSceneSerializer.h"

namespace ce::runtime
{

GameClientContent::GameClientContent(int clientNumber, juce::ValueTree sceneState,
                                     juce::String gameName, juce::String sceneName)
    // Unlike the editor's docked Scene Viewport, this window is a standalone
    // top-level window with no tab-hide/show to tear its GL context down --
    // self-attaching remains correct here (see ViewportComponent's
    // renderSurfaceHost_ comment for why the editor's viewport needs an
    // external host instead).
    : clientNumber_(clientNumber), world_(), viewport_(world_, interactions_, viewport_)
{
    // A run client owns an isolated World, but it begins with the exact
    // authored scene selected in the editor rather than a blank test world.
    ce::scene::EngineSceneSerializer::restoreScene(world_, sceneState);
    addAndMakeVisible(viewport_);
    viewport_.EnableFirstPersonMode();
    hud_.setText("CLIENT " + juce::String(clientNumber_) + "  |  " + gameName + " / " + sceneName,
                 juce::dontSendNotification);
    hud_.setColour(juce::Label::textColourId, juce::Colours::white);
    hud_.setColour(juce::Label::backgroundColourId, juce::Colour(0xaa10141a));
    hud_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hud_);
    startTimerHz(30);
}

void GameClientContent::resized()
{
    viewport_.setBounds(getLocalBounds());
    hud_.setBounds(12, 12, 250, 28);
}

void GameClientContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0b0e12));
}

void GameClientContent::timerCallback()
{
    if (playing_)
    {
        engine::FoundationGameplay::Step(world_, {}, 1.0f / 30.0f);
        engine::Simulation::Step(world_, 1.0f / 30.0f);
    }
    viewport_.repaint();
}

GameClientWindow::GameClientWindow(int clientNumber, juce::ValueTree sceneState,
                                   juce::String gameName, juce::String sceneName)
    : DocumentWindow(gameName + " - " + sceneName + " - Client " + juce::String(clientNumber),
                     juce::Colours::black,
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
    setContentOwned(new GameClientContent(clientNumber, sceneState, gameName, sceneName), true);
    centreWithSize(1280, 720);
    setVisible(true);
}

void GameClientWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace ce::runtime
