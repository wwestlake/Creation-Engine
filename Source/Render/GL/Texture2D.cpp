#include "Render/GL/Texture2D.h"

#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace juce::gl;

namespace ce::gl {

Texture2D::~Texture2D() {
    Release();
}

Texture2D::Texture2D(Texture2D&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        Release();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void Texture2D::Release() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
}

bool Texture2D::LoadFromFile(const juce::File& file, bool generateMipmaps) {
    if (!file.existsAsFile()) {
        std::cout << "[texture] file not found: " << file.getFullPathName() << std::endl;
        return false;
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    // Request the file's native channel count (0) rather than forcing
    // RGBA, so a 3-channel source stays 3-channel on upload.
    stbi_uc* pixels = stbi_load(file.getFullPathName().toRawUTF8(), &width, &height, &channelsInFile, 0);
    if (pixels == nullptr) {
        std::cout << "[texture] failed to decode " << file.getFullPathName() << ": " << stbi_failure_reason()
                   << std::endl;
        return false;
    }

    UploadCurrent(width, height, channelsInFile, pixels, generateMipmaps);
    stbi_image_free(pixels);

    std::cout << "[texture] loaded " << file.getFullPathName() << " (" << width << "x" << height << ", "
               << channelsInFile << " channels)" << std::endl;
    return true;
}

bool Texture2D::LoadFromMemory(const void* data, std::size_t sizeBytes, const juce::String& debugName,
                                bool generateMipmaps) {
    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(sizeBytes), &width,
                                             &height, &channelsInFile, 0);
    if (pixels == nullptr) {
        std::cout << "[texture] failed to decode " << (debugName.isNotEmpty() ? debugName : juce::String("<memory>"))
                   << ": " << stbi_failure_reason() << std::endl;
        return false;
    }

    UploadCurrent(width, height, channelsInFile, pixels, generateMipmaps);
    stbi_image_free(pixels);

    std::cout << "[texture] loaded " << (debugName.isNotEmpty() ? debugName : juce::String("<memory>")) << " ("
               << width << "x" << height << ", " << channelsInFile << " channels, from memory)" << std::endl;
    return true;
}

void Texture2D::CreateFromPixels(int width, int height, int channels, const std::uint8_t* pixels,
                                  bool generateMipmaps) {
    UploadCurrent(width, height, channels, pixels, generateMipmaps);
}

void Texture2D::UploadCurrent(int width, int height, int channels, const std::uint8_t* pixels,
                               bool generateMipmaps) {
    GLenum format = GL_RGB;
    switch (channels) {
        case 1:
            format = GL_RED;
            break;
        case 3:
            format = GL_RGB;
            break;
        case 4:
            format = GL_RGBA;
            break;
        default:
            std::cout << "[texture] unsupported channel count: " << channels << std::endl;
            return;
    }

    if (id_ == 0) {
        glGenTextures(1, &id_);
    }

    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), width, height, 0, format, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (generateMipmaps) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::Bind(unsigned int textureUnit) const {
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

} // namespace ce::gl
