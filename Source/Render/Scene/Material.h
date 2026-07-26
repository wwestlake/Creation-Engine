#pragma once

#include <JuceHeader.h>

#include "Render/GL/Texture2D.h"
#include "Render/Shaders/ShaderComposer.h"

namespace ce {

// Surface properties + shader variant selection for one drawable. Shader
// resolution goes through ShaderComposer so "with an albedo texture" vs
// "flat albedo color" is one variant define (USE_ALBEDO_TEXTURE) on the
// same pbr_lit program, not a hand-duplicated shader.
//
// Resolve() only re-asks the composer when the variant-affecting state
// (currently: whether a texture is bound) has actually changed since the
// last call — calling GetProgram() every frame regardless would still be
// correct (it's cache-backed) but would also re-log a cache hit every
// single frame, which is noise, not diagnostics.
class Material final {
public:
    juce::String vertexEntry = "programs/pbr_lit.vert";
    juce::String fragmentEntry = "programs/pbr_lit.frag";

    juce::Vector3D<float> albedo{ 0.7f, 0.15f, 0.15f };
    float metallic = 0.2f;
    float roughness = 0.4f;

    gl::Texture2D* albedoTexture = nullptr; // non-owning; asset ownership is future work.

    // AI5: set by AssetCatalog::AddFromModel when the source model was
    // skinned -- selects the USE_SKINNING shader variant the same way
    // hasTexture below selects USE_ALBEDO_TEXTURE. Asset-level, not
    // per-instance (every entity sharing this Material is the same
    // skinned mesh), so it lives here rather than on MeshRenderer/
    // Skeleton.
    bool isSkinned = false;

    juce::OpenGLShaderProgram* Resolve(ShaderComposer& composer, const juce::OpenGLContext& context);
    void ApplyUniforms(juce::OpenGLShaderProgram& program) const;

    // Call when the owning GL context is torn down (openGLContextClosing)
    // — the composer's cached programs die with the context, so a stale
    // cachedProgram_ pointer here would otherwise dangle until the next
    // context recreation silently reused it.
    void InvalidateCache() {
        cachedProgram_ = nullptr;
        hasResolvedOnce_ = false;
    }

private:
    juce::OpenGLShaderProgram* cachedProgram_ = nullptr;
    bool cachedHadTexture_ = false;
    bool cachedWasSkinned_ = false;
    bool hasResolvedOnce_ = false;
};

} // namespace ce
