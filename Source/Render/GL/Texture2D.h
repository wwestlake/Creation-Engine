#pragma once

#include <cstdint>
#include <vector>

#include <JuceHeader.h>

namespace ce::gl {

// RAII wrapper around a single GL_TEXTURE_2D object. Two ways in: decode
// a real image file from disk (stb_image — exercised for real starting
// with M4's glTF-embedded/external textures), or upload raw pixel data
// already in memory (used for procedurally generated textures, and for
// this milestone's demo checker texture).
class Texture2D final {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    // Decodes an image file (PNG/JPG/TGA/BMP/...) via stb_image and
    // uploads it. Returns false (and logs why) on failure — the texture
    // is left unchanged.
    bool LoadFromFile(const juce::File& file, bool generateMipmaps = true);

    // Uploads raw pixel data already in memory. `channels` must be 1
    // (R), 3 (RGB), or 4 (RGBA); data is tightly packed, row-major,
    // top-to-bottom.
    void CreateFromPixels(int width, int height, int channels, const std::uint8_t* pixels,
                           bool generateMipmaps = true);

    void Bind(unsigned int textureUnit) const;

    GLuint Id() const { return id_; }
    bool IsValid() const { return id_ != 0; }

private:
    void UploadCurrent(int width, int height, int channels, const std::uint8_t* pixels, bool generateMipmaps);
    void Release();

    GLuint id_ = 0;
};

} // namespace ce::gl
