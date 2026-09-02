#pragma once

#include "Import/AssetImporter.h"

namespace ce::import {

// Brings a Wavefront .obj model from disk into the live AssetCatalog
// (via Render/Import/ObjLoader.h's LoadObj) and places an entity for it
// straight into the scene -- same shape as GltfAssetImporter, just with
// no animation-import-options branch, since OBJ has no animation
// concept at all.
class ObjAssetImporter final : public AssetImporter {
public:
    juce::String DisplayName() const override { return "OBJ Model"; }
    std::vector<juce::String> SupportedExtensions() const override { return { "obj" }; }
    ImportResult Import(const juce::File& sourceFile, ImportContext& context) override;
    ImportResult Reimport(const juce::File& sourceFile, const creation::assets::AssetDescriptor& existingAsset,
                           ImportContext& context) override;
};

} // namespace ce::import
