#pragma once

#include <JuceHeader.h>

#include "Render/ViewportComponent.h"
#include "engine/simulation.h"
#include "engine/world.h"

namespace ce::runtime
{

class GameClientContent final : public juce::Component, private juce::Timer
{
public:
    GameClientContent(int clientNumber, juce::ValueTree sceneState, juce::String gameName, juce::String sceneName);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    int clientNumber_;
    engine::World world_;
    ViewportComponent viewport_;
    juce::Label hud_;
    bool playing_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameClientContent)
};

class GameClientWindow final : public juce::DocumentWindow
{
public:
    GameClientWindow(int clientNumber, juce::ValueTree sceneState, juce::String gameName, juce::String sceneName);
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GameClientWindow)
};

} // namespace ce::runtime
