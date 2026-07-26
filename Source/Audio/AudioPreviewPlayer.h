#pragma once

#include <memory>

#include <JuceHeader.h>

namespace ce::audio {

// Plays back a single decoded clip at a time, for the Import Hub's
// per-clip Play/Stop buttons. Does NOT open an audio device until the
// first Play() call -- an editor for a 3D engine has no business
// claiming the system's default audio output the moment it starts up,
// only once the user actually asks to hear something.
class AudioPreviewPlayer final {
public:
    AudioPreviewPlayer() = default;
    ~AudioPreviewPlayer();

    // Stops whatever was previously playing (if anything) and starts
    // playing buffer from the start. buffer is kept alive (shared
    // ownership) for as long as it's playing or until Stop()/another
    // Play() replaces it -- the caller doesn't need to keep its own
    // reference around.
    void Play(std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate);
    void Stop();
    bool IsPlaying() const;

private:
    void EnsureDeviceOpen();

    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer sourcePlayer_;
    juce::AudioTransportSource transportSource_;
    std::unique_ptr<juce::MemoryAudioSource> memorySource_;
    std::shared_ptr<juce::AudioBuffer<float>> currentBuffer_;
    bool deviceOpen_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreviewPlayer)
};

} // namespace ce::audio
