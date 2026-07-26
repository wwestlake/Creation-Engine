#include "Import/ImporterRegistry.h"

#include "Import/Importers/AudioAssetImporter.h"
#include "Import/Importers/GltfAssetImporter.h"

namespace ce::import {

void ImporterRegistry::Register(std::unique_ptr<AssetImporter> importer) {
    importers_.push_back(std::move(importer));
}

AssetImporter* ImporterRegistry::FindFor(const juce::File& file) const {
    const auto extension = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    for (const auto& importer : importers_) {
        for (const auto& supported : importer->SupportedExtensions()) {
            if (supported.equalsIgnoreCase(extension)) {
                return importer.get();
            }
        }
    }
    return nullptr;
}

void ImporterRegistry::RegisterBuiltins() {
    Register(std::make_unique<GltfAssetImporter>());
    Register(std::make_unique<AudioAssetImporter>());
}

} // namespace ce::import
