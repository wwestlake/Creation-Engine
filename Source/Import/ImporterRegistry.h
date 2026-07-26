#pragma once

#include <memory>
#include <vector>

#include "Import/AssetImporter.h"

namespace ce::import {

// Owns every registered AssetImporter and picks the right one for a
// given file by extension. RegisterBuiltins() (ImporterRegistry.cpp)
// registers every format the engine ships with; nothing else in the app
// needs to know the concrete importer types, only this registry.
class ImporterRegistry final {
public:
    void Register(std::unique_ptr<AssetImporter> importer);

    // Returns nullptr if no registered importer claims file's extension.
    AssetImporter* FindFor(const juce::File& file) const;

    const std::vector<std::unique_ptr<AssetImporter>>& Importers() const { return importers_; }

    // Registers every importer the engine currently ships with (glTF
    // today; audio/texture/video/armature land as their own milestones).
    void RegisterBuiltins();

private:
    std::vector<std::unique_ptr<AssetImporter>> importers_;
};

} // namespace ce::import
