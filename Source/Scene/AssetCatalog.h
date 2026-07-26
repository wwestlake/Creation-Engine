#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>

#include "assets/VirtualFileSystem.h"
#include "Render/GL/Texture2D.h"
#include "Render/Scene/Material.h"
#include "Render/Scene/Mesh.h"
#include "Render/Scene/Vertex.h"

namespace ce::scene {

// Owns GPU-resident mesh/material/texture assets behind shared_ptr, so
// multiple placed entities can reference the same asset without
// duplicating GPU resources. Lifetime is tied to the GL context — build
// it in newOpenGLContextCreated, drop it in openGLContextClosing, same
// rule every other GL-resource owner in this render layer follows.
//
// Building a Mesh needs a live GL context (vertex/index buffer upload);
// building a Material does not (it only becomes a real shader program
// lazily, the first time something calls Material::Resolve() at draw
// time) — but both happen here together for one asset at a time, so
// callers never have to know which part needs the context and which
// doesn't.
class AssetCatalog final {
public:
    struct Asset {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
    };

    // Loads the built-in demo set: a procedural cube, a procedural
    // sphere, and the BoxTextured glTF asset read through vfs (expects
    // assets/packages/base.zip already mounted).
    void LoadBuiltins(assets::VirtualFileSystem& vfs);

    const Asset* Find(const juce::String& name) const;
    const std::vector<juce::String>& Names() const { return names_; }

private:
    void AddProcedural(const juce::String& name, const std::vector<Vertex>& vertices,
                        const std::vector<GLuint>& indices, juce::Vector3D<float> albedo);

    std::vector<std::unique_ptr<gl::Texture2D>> ownedTextures_;
    std::unordered_map<std::string, Asset> assets_;
    std::vector<juce::String> names_;
};

} // namespace ce::scene
