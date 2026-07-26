#include "MainComponent.h"

MainComponent::MainComponent()
    : viewport_(world_),
      hierarchyPanel_(world_, viewport_),
      transformPanel_(world_),
      importPanel_(world_, viewport_),
      lightPanel_(viewport_) {
    addAndMakeVisible(transportBar_);
    transportBar_.onPlay = [this] { SetPlaying(true); };
    transportBar_.onPause = [this] { SetPlaying(false); };
    transportBar_.onStop = [this] {
        SetPlaying(false);
        world_.ResetTick();
        viewport_.ResetDemoEntityTransform();
        transportBar_.SetStatusText("Stopped");
    };

    addAndMakeVisible(viewModeBar_);
    viewModeBar_.onModeSelected = [this](ce::WorkspaceMode mode) { SetActiveMode(mode); };

    addAndMakeVisible(hierarchyPanel_);
    hierarchyPanel_.onSelectionChanged = [this](entt::entity entity) { transformPanel_.SetSelectedEntity(entity); };
    addAndMakeVisible(viewport_);

    inspectorTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    inspectorTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(inspectorTitle_);

    tickLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(tickLabel_);

    addAndMakeVisible(transformPanel_);

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

    addAndMakeVisible(materialsPanel_);
    addAndMakeVisible(importPanel_);
    addAndMakeVisible(serverPanel_);
    addAndMakeVisible(settingsPanel_);

    SetActiveMode(ce::WorkspaceMode::Scene);
    SetPlaying(false);

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

    transportBar_.setBounds(bounds.removeFromTop(92));
    viewModeBar_.setBounds(bounds.removeFromTop(56));

    const auto contentArea = bounds;

    // Scene mode content — laid out even while hidden, so switching back
    // to Scene doesn't need a relayout.
    auto sceneArea = contentArea;
    hierarchyPanel_.setBounds(sceneArea.removeFromLeft(220).reduced(4));
    auto inspectorBounds = sceneArea.removeFromRight(300).reduced(12);
    inspectorTitle_.setBounds(inspectorBounds.removeFromTop(28));
    tickLabel_.setBounds(inspectorBounds.removeFromTop(24));

    inspectorBounds.removeFromTop(12);
    transformPanel_.setBounds(inspectorBounds.removeFromTop(ce::TransformPanel::kPreferredHeight));

    inspectorBounds.removeFromTop(12);
    roughnessLabel_.setBounds(inspectorBounds.removeFromTop(18));
    roughnessSlider_.setBounds(inspectorBounds.removeFromTop(24));
    inspectorBounds.removeFromTop(8);
    metallicLabel_.setBounds(inspectorBounds.removeFromTop(18));
    metallicSlider_.setBounds(inspectorBounds.removeFromTop(24));

    inspectorBounds.removeFromTop(16);
    lightPanel_.setBounds(inspectorBounds.removeFromTop(lightPanel_.PreferredHeight()));

    viewport_.setBounds(sceneArea);

    materialsPanel_.setBounds(contentArea);
    importPanel_.setBounds(contentArea);
    serverPanel_.setBounds(contentArea);
    settingsPanel_.setBounds(contentArea);
}

void MainComponent::SetActiveMode(ce::WorkspaceMode mode) {
    activeMode_ = mode;

    const bool showScene = mode == ce::WorkspaceMode::Scene;
    hierarchyPanel_.setVisible(showScene);
    viewport_.setVisible(showScene);
    inspectorTitle_.setVisible(showScene);
    tickLabel_.setVisible(showScene);
    transformPanel_.setVisible(showScene);
    roughnessLabel_.setVisible(showScene);
    roughnessSlider_.setVisible(showScene);
    metallicLabel_.setVisible(showScene);
    metallicSlider_.setVisible(showScene);
    lightPanel_.setVisible(showScene);

    materialsPanel_.setVisible(mode == ce::WorkspaceMode::Materials);
    importPanel_.setVisible(mode == ce::WorkspaceMode::Assets);
    serverPanel_.setVisible(mode == ce::WorkspaceMode::Server);
    settingsPanel_.setVisible(mode == ce::WorkspaceMode::Settings);
}

void MainComponent::SetPlaying(bool playing) {
    isPlaying_ = playing;
    transportBar_.SetPlaying(isPlaying_);
    transportBar_.SetStatusText(isPlaying_ ? "Playing" : "Editing");
}

void MainComponent::timerCallback() {
    if (isPlaying_) {
        world_.AdvanceTick();
    }
    tickLabel_.setText("tick " + juce::String(world_.CurrentTick()), juce::dontSendNotification);
    hierarchyPanel_.Refresh();
    transformPanel_.Refresh();
}
