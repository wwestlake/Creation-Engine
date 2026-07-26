#include "MainComponent.h"

MainComponent::MainComponent() : viewport_(world_), lightPanel_(viewport_) {
    addAndMakeVisible(viewport_);

    inspectorTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    inspectorTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(inspectorTitle_);

    tickLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(tickLabel_);

    roughnessLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(roughnessLabel_);
    roughnessSlider_.setRange(0.05, 1.0);
    roughnessSlider_.setValue(viewport_.Roughness(), juce::dontSendNotification);
    roughnessSlider_.onValueChange = [this] { viewport_.SetRoughness(static_cast<float>(roughnessSlider_.getValue())); };
    addAndMakeVisible(roughnessSlider_);

    metallicLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(metallicLabel_);
    metallicSlider_.setRange(0.0, 1.0);
    metallicSlider_.setValue(viewport_.Metallic(), juce::dontSendNotification);
    metallicSlider_.onValueChange = [this] { viewport_.SetMetallic(static_cast<float>(metallicSlider_.getValue())); };
    addAndMakeVisible(metallicSlider_);

    addAndMakeVisible(lightPanel_);

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
    auto inspectorBounds = bounds.removeFromRight(300).reduced(12);
    inspectorTitle_.setBounds(inspectorBounds.removeFromTop(28));
    tickLabel_.setBounds(inspectorBounds.removeFromTop(24));

    inspectorBounds.removeFromTop(12);
    roughnessLabel_.setBounds(inspectorBounds.removeFromTop(18));
    roughnessSlider_.setBounds(inspectorBounds.removeFromTop(24));
    inspectorBounds.removeFromTop(8);
    metallicLabel_.setBounds(inspectorBounds.removeFromTop(18));
    metallicSlider_.setBounds(inspectorBounds.removeFromTop(24));

    inspectorBounds.removeFromTop(16);
    lightPanel_.setBounds(inspectorBounds.removeFromTop(lightPanel_.PreferredHeight()));

    viewport_.setBounds(bounds);
}

void MainComponent::timerCallback() {
    world_.AdvanceTick();
    tickLabel_.setText("tick " + juce::String(world_.CurrentTick()), juce::dontSendNotification);
}
