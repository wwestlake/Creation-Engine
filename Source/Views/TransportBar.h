#pragma once

#include <JuceHeader.h>

#include "Views/TransportLookAndFeel.h"

namespace ce {

// Top bar: logo (top right, per explicit direction — Creation Station's
// sits top-left, this is a deliberate difference not an oversight),
// title, status text, and Play/Pause/Stop — the game/simulation
// transport (spec section 3.3's "play-in-viewport" / section 1's
// editor-vs-runtime mode split), not an audio transport. Same color
// scheme as Creation Station's TransportBar (background #0f1115,
// divider #242a36, cyan accent).
class TransportBar final : public juce::Component {
public:
    TransportBar();
    ~TransportBar() override;

    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;

    // Keeps the Play button's toggled/lit state in sync with whatever
    // owns the actual play/pause/stop state machine.
    void SetPlaying(bool isPlaying);
    void SetStatusText(const juce::String& text);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    TransportLookAndFeel transportLookAndFeel_;

    juce::Image logoImage_;
    juce::Label titleLabel_{ {}, "Creation Engine" };
    juce::Label statusLabel_;

    juce::TextButton playButton_{ "play" };
    juce::TextButton pauseButton_{ "pause" };
    juce::TextButton stopButton_{ "stop" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace ce
