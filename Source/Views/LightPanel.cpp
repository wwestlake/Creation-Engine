#include "Views/LightPanel.h"

namespace ce {

// --- PointLightRow ---------------------------------------------------

LightPanel::PointLightRow::PointLightRow(ViewportComponent& viewport, int index, std::function<void()> onRemoved)
    : viewport_(viewport), index_(index), onRemoved_(std::move(onRemoved)) {
    const PointLight current = viewport_.GetPointLight(index_);

    title_.setText("Point " + juce::String(index_ + 1), juce::dontSendNotification);
    title_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title_);

    intensityLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(intensityLabel_);
    intensitySlider_.setRange(0.0, 20.0);
    intensitySlider_.setValue(current.intensity, juce::dontSendNotification);
    intensitySlider_.onValueChange = [this] { PushToViewport(); };
    addAndMakeVisible(intensitySlider_);

    positionLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(positionLabel_);

    for (auto* slider : { &positionXSlider_, &positionYSlider_, &positionZSlider_ }) {
        slider->setRange(-5.0, 5.0);
        slider->onValueChange = [this] { PushToViewport(); };
        addAndMakeVisible(*slider);
    }
    positionXSlider_.setValue(current.position.x, juce::dontSendNotification);
    positionYSlider_.setValue(current.position.y, juce::dontSendNotification);
    positionZSlider_.setValue(current.position.z, juce::dontSendNotification);

    removeButton_.onClick = [this] {
        if (onRemoved_) {
            onRemoved_();
        }
    };
    addAndMakeVisible(removeButton_);
}

void LightPanel::PointLightRow::PushToViewport() {
    PointLight light;
    light.intensity = static_cast<float>(intensitySlider_.getValue());
    light.position = { static_cast<float>(positionXSlider_.getValue()), static_cast<float>(positionYSlider_.getValue()),
                        static_cast<float>(positionZSlider_.getValue()) };
    light.color = viewport_.GetPointLight(index_).color; // color editing is a follow-up, keep whatever it already is.
    viewport_.SetPointLight(index_, light);
}

void LightPanel::PointLightRow::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void LightPanel::PointLightRow::resized() {
    auto bounds = getLocalBounds().reduced(0, 4);

    auto titleRow = bounds.removeFromTop(20);
    title_.setBounds(titleRow.removeFromLeft(titleRow.getWidth() - 70));
    removeButton_.setBounds(titleRow);

    intensityLabel_.setBounds(bounds.removeFromTop(16));
    intensitySlider_.setBounds(bounds.removeFromTop(22));

    bounds.removeFromTop(4);
    positionLabel_.setBounds(bounds.removeFromTop(16));
    auto positionRow = bounds.removeFromTop(22);
    const int third = positionRow.getWidth() / 3;
    positionXSlider_.setBounds(positionRow.removeFromLeft(third));
    positionYSlider_.setBounds(positionRow.removeFromLeft(third));
    positionZSlider_.setBounds(positionRow);
}

// --- LightPanel --------------------------------------------------------

LightPanel::LightPanel(ViewportComponent& viewport) : viewport_(viewport) {
    sunTitle_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    sunTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(sunTitle_);

    sunIntensityLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(sunIntensityLabel_);
    sunIntensitySlider_.setRange(0.0, 10.0);
    sunIntensitySlider_.setValue(viewport_.GetSunLight().intensity, juce::dontSendNotification);
    sunIntensitySlider_.onValueChange = [this] {
        DirectionalLight light = viewport_.GetSunLight();
        light.intensity = static_cast<float>(sunIntensitySlider_.getValue());
        viewport_.SetSunLight(light);
    };
    addAndMakeVisible(sunIntensitySlider_);

    pointLightsTitle_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    pointLightsTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(pointLightsTitle_);

    addPointLightButton_.onClick = [this] {
        viewport_.AddPointLight();
        RebuildPointLightRows();
    };
    addAndMakeVisible(addPointLightButton_);

    RebuildPointLightRows();
}

void LightPanel::RebuildPointLightRows() {
    pointLightRows_.clear();

    const int count = viewport_.GetPointLightCount();
    for (int i = 0; i < count; ++i) {
        auto* row = pointLightRows_.add(new PointLightRow(viewport_, i, [this] {
            // Rows are rebuilt wholesale on any add/remove so every
            // remaining row's index stays correct — see class comment.
            RebuildPointLightRows();
        }));
        addAndMakeVisible(row);
    }

    addPointLightButton_.setEnabled(count < kMaxPointLights);

    if (auto* parent = getParentComponent()) {
        parent->resized();
    }
    resized();
}

int LightPanel::PreferredHeight() const {
    const int sunHeight = 20 + 16 + 22 + 8; // title + label + slider + gap
    const int pointLightsHeaderHeight = 16 + 8;
    const int rowsHeight = pointLightRows_.size() * PointLightRow::kHeight;
    const int addButtonHeight = 28;
    return sunHeight + pointLightsHeaderHeight + rowsHeight + addButtonHeight;
}

void LightPanel::paint(juce::Graphics&) {}

void LightPanel::resized() {
    auto bounds = getLocalBounds();

    sunTitle_.setBounds(bounds.removeFromTop(20));
    sunIntensityLabel_.setBounds(bounds.removeFromTop(16));
    sunIntensitySlider_.setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(8);

    pointLightsTitle_.setBounds(bounds.removeFromTop(16));
    bounds.removeFromTop(8);

    for (auto* row : pointLightRows_) {
        row->setBounds(bounds.removeFromTop(PointLightRow::kHeight));
    }

    addPointLightButton_.setBounds(bounds.removeFromTop(28));
}

} // namespace ce
