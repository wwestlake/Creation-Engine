#include "Import/Importers/AudioAssetImporter.h"

#include "Assets/ProjectContentAssetStore.h"
#include "Audio/AudioCatalog.h"

namespace ce::import {

ImportResult AudioAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.audioCatalog == nullptr || context.audioFormatManager == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("Audio import needs an open game and live audio catalog.");
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();

    creation::assets::AssetDescriptor sourceDescriptor;
    juce::String persistenceError;
    if (! assets::ProjectContentAssetStore::importSource(*context.projectSession, context.gameAssetRoot, sourceFile,
                                                          creation::assets::AssetKind::audio, "Audio",
                                                          { "audio" }, sourceDescriptor, persistenceError))
        return ImportResult::Failed("Could not store audio source in project content: " + persistenceError);
    if (!context.audioCatalog->AddFromFile(assetName, sourceFile, *context.audioFormatManager)) {
        return ImportResult::Failed("Failed to decode " + sourceFile.getFileName() + " (see log for details).");
    }

    const auto asset = context.audioCatalog->Find(assetName);
    return ImportResult::Ok(assetName + " stored in project content (" + juce::String(asset.lengthSeconds, 1) + "s, " +
                             juce::String(static_cast<int>(asset.sampleRate)) + "Hz).");
}

} // namespace ce::import
