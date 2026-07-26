#pragma once

#include "Import/AssetImporter.h"

namespace ce::import {

// Decodes a standalone image file (PNG/JPG/TGA/BMP/HDR, via stb_image
// through Texture2D::LoadFromFile) and registers it in the live
// AssetCatalog as a textured cube -- reuses the existing procedural cube
// mesh rather than introducing a new quad/plane primitive just for this,
// so the result is immediately placeable and visible via SC5's "+ Add"
// menu, the same as any other catalog asset.
//
// .hdr files are accepted but decoded through the same LDR (8-bit) path
// as everything else -- stb_image tone-maps them down automatically, so
// nothing crashes, but true float/extended-range HDR data isn't
// preserved. Real HDR (GL_RGB16F et al.) is a legitimate feature, but
// there's no HDR-consuming system yet (environment lighting, a skybox)
// to justify the extra Texture2D work for; it can be added when one
// exists to actually use the extra range.
class TextureAssetImporter final : public AssetImporter {
public:
    juce::String DisplayName() const override { return "Texture"; }
    std::vector<juce::String> SupportedExtensions() const override {
        return { "png", "jpg", "jpeg", "tga", "bmp", "hdr" };
    }
    ImportResult Import(const juce::File& sourceFile, ImportContext& context) override;
};

} // namespace ce::import
