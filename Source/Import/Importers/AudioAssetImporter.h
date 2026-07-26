#pragma once

#include "Import/AssetImporter.h"

namespace ce::import {

// Decodes a WAV/AIFF/FLAC file via juce::AudioFormatManager and
// registers it in the live AudioCatalog (Source/Audio/AudioCatalog.h),
// under a name derived from the file name. Unlike GltfAssetImporter,
// there's no GPU-resource step -- decoding is the whole job, so this
// runs entirely on whichever thread calls Import() (the message thread,
// via the Import Hub's drag-and-drop).
class AudioAssetImporter final : public AssetImporter {
public:
    juce::String DisplayName() const override { return "Audio Clip"; }
    std::vector<juce::String> SupportedExtensions() const override { return { "wav", "aiff", "aif", "flac" }; }
    ImportResult Import(const juce::File& sourceFile, ImportContext& context) override;
};

} // namespace ce::import
