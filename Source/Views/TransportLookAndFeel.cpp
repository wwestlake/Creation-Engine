#include "Views/TransportLookAndFeel.h"

namespace ce {

void TransportLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour&, bool isMouseOverButton, bool isButtonDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
    const bool isToggle = button.getToggleState();
    const bool isPlay = button.getButtonText() == "play";
    const bool isPrimaryActive = isToggle && isPlay;
    const auto accent = juce::Colour(0xff59dfff);
    auto fill = juce::Colour(0xff17222c);

    if (isToggle) {
        fill = accent.withAlpha(isPrimaryActive ? 0.48f : 0.25f).overlaidWith(juce::Colour(0xff13202b));
    } else if (isButtonDown) {
        fill = accent.withAlpha(0.20f).overlaidWith(fill);
    } else if (isMouseOverButton) {
        fill = accent.withAlpha(0.12f).overlaidWith(fill);
    }

    g.setColour(accent.withAlpha(isToggle ? (isPrimaryActive ? 0.62f : 0.35f) : isMouseOverButton ? 0.35f : 0.14f));
    g.fillRoundedRectangle(bounds.expanded(isPrimaryActive ? 4.0f : 2.0f), 13.0f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 11.0f);

    g.setColour(accent.withAlpha(isToggle ? 1.0f : 0.62f));
    g.drawRoundedRectangle(bounds, 11.0f, isToggle ? (isPrimaryActive ? 2.8f : 2.0f) : 1.3f);
}

void TransportLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton,
                                           bool isButtonDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(8.0f, 7.0f);

    g.setColour(button.getToggleState()
                    ? juce::Colours::white
                    : (isButtonDown ? juce::Colour(0xffeaf6ff)
                                     : isMouseOverButton ? juce::Colour(0xffdcecff) : juce::Colour(0xffb8c4d5)));

    DrawIcon(g, bounds, button.getButtonText());
}

void TransportLookAndFeel::DrawIcon(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& iconName) {
    const auto centre = bounds.getCentre();
    const auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

    if (iconName == "play") {
        juce::Path path;
        path.addTriangle(centre.x - size * 0.22f, centre.y - size * 0.32f, centre.x - size * 0.22f,
                          centre.y + size * 0.32f, centre.x + size * 0.32f, centre.y);
        g.fillPath(path);
        return;
    }

    if (iconName == "pause") {
        const auto barWidth = size * 0.17f;
        const auto gap = size * 0.10f;
        const auto height = size * 0.62f;
        g.fillRoundedRectangle(centre.x - gap - barWidth, centre.y - height * 0.5f, barWidth, height, 1.5f);
        g.fillRoundedRectangle(centre.x + gap, centre.y - height * 0.5f, barWidth, height, 1.5f);
        return;
    }

    if (iconName == "stop") {
        const auto square = juce::Rectangle<float>(size * 0.55f, size * 0.55f).withCentre(centre);
        g.fillRoundedRectangle(square, 2.0f);
        return;
    }

    g.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    g.drawText(iconName, bounds.toNearestInt(), juce::Justification::centred, true);
}

} // namespace ce
