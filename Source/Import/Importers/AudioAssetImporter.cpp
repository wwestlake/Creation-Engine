#include "Import/Importers/AudioAssetImporter.h"

#include "Audio/AudioCatalog.h"

namespace ce::import {

ImportResult AudioAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.audioCatalog == nullptr || context.audioFormatManager == nullptr) {
        return ImportResult::Failed("Audio import needs a live audio catalog.");
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();
    if (!context.audioCatalog->AddFromFile(assetName, sourceFile, *context.audioFormatManager)) {
        return ImportResult::Failed("Failed to decode " + sourceFile.getFileName() + " (see log for details).");
    }

    const auto asset = context.audioCatalog->Find(assetName);
    return ImportResult::Ok(assetName + " imported (" + juce::String(asset.lengthSeconds, 1) + "s, " +
                             juce::String(static_cast<int>(asset.sampleRate)) + "Hz).");
}

} // namespace ce::import
