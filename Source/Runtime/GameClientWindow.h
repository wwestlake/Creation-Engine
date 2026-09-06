#pragma once

#include <JuceHeader.h>

#include "Interaction/EditorInteraction.h"
#include "Physics/PhysicsWorld.h"
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
    // See MainComponent.h's own physicsWorld_ comment -- same lifetime
    // rule (lives alongside world_, never owned by a dockable panel).
    ce::physics::PhysicsWorld physicsWorld_;
    double lastPhysicsAdvanceSeconds_ = 0.0;
    interaction::EditorInteraction interactions_{ world_ };
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
