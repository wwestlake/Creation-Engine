#pragma once

#include "Import/AssetImporter.h"

namespace ce::import {

class FbxAssetImporter final : public AssetImporter {
public:
    juce::String DisplayName() const override { return "FBX Model"; }
    std::vector<juce::String> SupportedExtensions() const override { return { "fbx" }; }
    ImportResult Import(const juce::File& sourceFile, ImportContext& context) override;
};

} // namespace ce::import
