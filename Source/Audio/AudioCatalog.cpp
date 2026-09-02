#include "Audio/AudioCatalog.h"

#include <algorithm>
#include <iostream>

namespace ce::audio {

bool AudioCatalog::AddFromFile(const juce::String& name, const juce::File& file,
                                juce::AudioFormatManager& formatManager) {
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) {
        std::cout << "[audio] failed to open " << file.getFullPathName() << " -- no matching format reader."
                   << std::endl;
        return false;
    }

    const auto numChannels = static_cast<int>(reader->numChannels);
    const auto numSamples = static_cast<int>(reader->lengthInSamples);

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);
    reader->read(buffer.get(), 0, numSamples, 0, true, true);

    AudioAsset asset;
    asset.name = name;
    asset.buffer = buffer;
    asset.sampleRate = reader->sampleRate;
    asset.lengthSeconds = reader->sampleRate > 0.0 ? static_cast<double>(numSamples) / reader->sampleRate : 0.0;

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (assets_.find(name.toStdString()) == assets_.end()) {
            names_.push_back(name);
        }
        assets_[name.toStdString()] = std::move(asset);
    }

    std::cout << "[audio] loaded " << file.getFileName() << " (" << numChannels << "ch, " << reader->sampleRate
              << "Hz, " << (static_cast<double>(numSamples) / reader->sampleRate) << "s)" << std::endl;
    return true;
}

AudioAsset AudioCatalog::Find(const juce::String& name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = assets_.find(name.toStdString());
    return it == assets_.end() ? AudioAsset{} : it->second;
}

std::vector<juce::String> AudioCatalog::Names() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return names_;
}

bool AudioCatalog::Remove(const juce::String& name) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (assets_.erase(name.toStdString()) == 0) return false;
    names_.erase(std::remove(names_.begin(), names_.end(), name), names_.end());
    return true;
}

} // namespace ce::audio
