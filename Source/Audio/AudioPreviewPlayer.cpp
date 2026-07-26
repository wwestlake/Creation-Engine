#include "Audio/AudioPreviewPlayer.h"

namespace ce::audio {

AudioPreviewPlayer::~AudioPreviewPlayer() {
    Stop();
    if (deviceOpen_) {
        deviceManager_.removeAudioCallback(&sourcePlayer_);
        sourcePlayer_.setSource(nullptr);
    }
}

void AudioPreviewPlayer::EnsureDeviceOpen() {
    if (deviceOpen_) {
        return;
    }
    deviceManager_.initialiseWithDefaultDevices(0, 2);
    sourcePlayer_.setSource(&transportSource_);
    deviceManager_.addAudioCallback(&sourcePlayer_);
    deviceOpen_ = true;
}

void AudioPreviewPlayer::Play(std::shared_ptr<juce::AudioBuffer<float>> buffer, double sampleRate) {
    if (buffer == nullptr) {
        return;
    }
    EnsureDeviceOpen();
    Stop();

    currentBuffer_ = std::move(buffer);
    memorySource_ = std::make_unique<juce::MemoryAudioSource>(*currentBuffer_, false, false);
    transportSource_.setSource(memorySource_.get(), 0, nullptr, sampleRate, currentBuffer_->getNumChannels());
    transportSource_.start();
}

void AudioPreviewPlayer::Stop() {
    transportSource_.stop();
    transportSource_.setSource(nullptr);
    memorySource_.reset();
    currentBuffer_.reset();
}

bool AudioPreviewPlayer::IsPlaying() const {
    return transportSource_.isPlaying();
}

} // namespace ce::audio
