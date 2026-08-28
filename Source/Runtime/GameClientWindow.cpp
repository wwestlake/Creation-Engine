#include "GameClientWindow.h"

#include <creation/ui/CreationSuiteLogos.h>

namespace ce::runtime
{

GameClientContent::GameClientContent(int clientNumber)
    : clientNumber_(clientNumber), world_(), viewport_(world_)
{
    addAndMakeVisible(viewport_);
    hud_.setText("CLIENT " + juce::String(clientNumber_) + "  |  LOCAL SESSION", juce::dontSendNotification);
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
        engine::Simulation::Step(world_, 1.0f / 30.0f);
    viewport_.repaint();
}

GameClientWindow::GameClientWindow(int clientNumber)
    : DocumentWindow("Game Client " + juce::String(clientNumber),
                     juce::Colours::black,
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
    setContentOwned(new GameClientContent(clientNumber), true);
    centreWithSize(1280, 720);
    setVisible(true);
}

void GameClientWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace ce::runtime
