#include "MainComponent.h"

MainComponent::MainComponent() {
    addAndMakeVisible(viewport_);

    inspectorTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    inspectorTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(inspectorTitle_);

    tickLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(tickLabel_);

    setSize(1400, 900);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void MainComponent::resized() {
    auto bounds = getLocalBounds();

    // Inspector docked to the right, viewport fills the rest — both
    // composited in this one window, per section 3.4. (A translucent
    // overlay-on-top-of-viewport layout is a UI polish choice for later,
    // not a technical constraint of this arrangement.)
    auto inspectorBounds = bounds.removeFromRight(280).reduced(12);
    inspectorTitle_.setBounds(inspectorBounds.removeFromTop(28));
    tickLabel_.setBounds(inspectorBounds.removeFromTop(24));

    viewport_.setBounds(bounds);
}

void MainComponent::timerCallback() {
    world_.AdvanceTick();
    tickLabel_.setText("tick " + juce::String(world_.CurrentTick()), juce::dontSendNotification);
}
