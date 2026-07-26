#pragma once

#include "Import/AssetImporter.h"

namespace ce::import {

// Brings a glTF/GLB model from disk into the live AssetCatalog, under a
// name derived from the file name, via the same disk-mode GltfLoader::
// LoadGltf extraction M4 already built for the demo scene -- just reached
// from a dropped file instead of a hardcoded path. GPU resource creation
// (Mesh::Upload, Texture2D::LoadFromFile) is dispatched onto the render
// thread via context.viewport->RunOnGLThread, since Import() itself runs
// wherever the caller (the Import Hub's drag-and-drop handler) calls it
// from -- the message thread, not the render thread.
class GltfAssetImporter final : public AssetImporter {
public:
    juce::String DisplayName() const override { return "glTF Model"; }
    std::vector<juce::String> SupportedExtensions() const override { return { "gltf", "glb" }; }
    ImportResult Import(const juce::File& sourceFile, ImportContext& context) override;
};

} // namespace ce::import
