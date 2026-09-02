#include "Import/Importers/AudioAssetImporter.h"

#include <creation/assets/ProjectAssetService.h>

#include "Assets/ProjectContentAssetStore.h"
#include "Audio/AudioCatalog.h"

namespace ce::import {

ImportResult AudioAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.audioCatalog == nullptr || context.audioFormatManager == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("Audio import needs an open game and live audio catalog.");
    }

    const juce::String assetName =
        context.pendingDisplayName.isNotEmpty() ? context.pendingDisplayName : sourceFile.getFileNameWithoutExtension();

    juce::StringArray tags{ "audio" };
    tags.addArray(context.pendingTags);

    creation::assets::AssetDescriptor sourceDescriptor;
    juce::String persistenceError;
    if (! assets::ProjectContentAssetStore::importSource(*context.projectSession, context.gameAssetRoot, sourceFile,
                                                          creation::assets::AssetKind::audio, "Audio", tags,
                                                          sourceDescriptor, persistenceError,
                                                          context.pendingDisplayName, context.pendingDescription))
        return ImportResult::Failed("Could not store audio source in project content: " + persistenceError);
    if (!context.audioCatalog->AddFromFile(assetName, sourceFile, *context.audioFormatManager)) {
        return ImportResult::Failed("Failed to decode " + sourceFile.getFileName() + " (see log for details).");
    }

    const auto asset = context.audioCatalog->Find(assetName);
    return ImportResult::Ok(assetName + " stored in project content (" + juce::String(asset.lengthSeconds, 1) + "s, " +
                             juce::String(static_cast<int>(asset.sampleRate)) + "Hz).");
}

ImportResult AudioAssetImporter::Reimport(const juce::File& sourceFile,
                                          const creation::assets::AssetDescriptor& existingAsset,
                                          ImportContext& context) {
    if (context.audioCatalog == nullptr || context.audioFormatManager == nullptr || context.projectSession == nullptr) {
        return ImportResult::Failed("Audio reimport needs an open game and live audio catalog.");
    }

    creation::assets::ProjectAssetService::ImportOptions overrides;
    creation::assets::AssetDescriptor newDescriptor;
    juce::String persistenceError;
    if (! creation::assets::ProjectAssetService::createNewVersion(*context.projectSession, existingAsset, sourceFile,
                                                                   overrides, newDescriptor, persistenceError))
        return ImportResult::Failed("Could not save the new version: " + persistenceError);
    if (! context.projectSession->commit(persistenceError))
        return ImportResult::Failed("New version saved, but the project manifest could not be committed: " + persistenceError);

    if (!context.audioCatalog->AddFromFile(existingAsset.displayName, sourceFile, *context.audioFormatManager)) {
        return ImportResult::Failed("Failed to decode " + sourceFile.getFileName() + " (see log for details).");
    }

    const auto asset = context.audioCatalog->Find(existingAsset.displayName);
    return ImportResult::Ok(existingAsset.displayName + " updated to a new version (" +
                             juce::String(asset.lengthSeconds, 1) + "s, " +
                             juce::String(static_cast<int>(asset.sampleRate)) + "Hz).");
}

} // namespace ce::import
