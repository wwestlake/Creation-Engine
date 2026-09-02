#pragma once

#include <memory>
#include <unordered_map>

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
    // The registry name this Material was created/found under
    // (AssetCatalog::GetOrCreateMaterial) -- lets any UI that only has a
    // Material* (a mesh asset's slot, an entity's resolved renderer)
    // display or look up "which named material is this" without a
    // separate reverse-lookup pass over the registry. Empty for a
    // Material that was never registered by name (e.g. a fixed-path
    // primitive's default).
    juce::String assetName;

    // The editable graph that produced compiledMaterialSource below,
    // serialized via ce::node_system::SerializeGraph (shared/NodeSystem's
    // .frgraph text format) -- NOT regenerated from compiledMaterialSource
    // (GLSL isn't reversible back into a graph). This is what makes
    // "open this material from the list and keep editing it" actually
    // work, instead of reopening always handing you a blank default
    // graph disconnected from what's actually compiled in. Empty for a
    // material that was created but never saved from the graph editor.
    juce::String savedGraphSource;

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

    // A compiled material graph's generated GLSL (see
    // ce::material::CompileMaterialGraph -- declarations + an
    // EvaluateMaterial function + an EvaluateWorldPositionOffset
    // function), or empty for the fixed albedo/metallic/roughness path
    // above. When set, Resolve() requests programs/material_host.vert +
    // programs/material_host.frag with this spliced into both (same text,
    // each host calls only the function meaningful to its own stage) --
    // instead of the fixed pbr_lit pair -- and ApplyUniforms() skips the
    // fixed uniforms entirely, since EvaluateMaterial computes them
    // internally. See docs/MATERIAL_SYSTEM_PLAN.md.
    juce::String compiledMaterialSource;

    // A compiled graph's Scalar/Vector Parameter nodes (ce::material::
    // MaterialCompileResult::source.parameters), keyed by name -- seeded
    // from each parameter's own "default" input when the graph compiles
    // (MaterialGraphPanel::compileAndApply), then a plain mutable current
    // value exactly like albedo/metallic/roughness already are above.
    // Rendered into the matching uMaterial_<name> uniform every frame
    // (ViewportComponent::renderOpenGL) -- a per-entity
    // engine::MaterialParameterOverrides value wins over this one when
    // present, the same "shared asset vs per-instance override" split
    // Tint already uses for albedo, so FRust driving one entity's color
    // never bleeds into every other entity sharing this Material.
    std::unordered_map<std::string, float> parameterFloatValues;
    std::unordered_map<std::string, juce::Vector3D<float>> parameterColorValues;

    // A compiled graph's Texture Sample nodes (ce::material::
    // MaterialCompileResult::source.textures), keyed by the compiler's
    // generated sampler uniform name -- populated by MaterialGraphPanel
    // after a successful compile (it resolves each texture's file path
    // through AssetCatalog::GetOrLoadTexture, this Material just holds
    // the result). Bound at draw time (ViewportComponent::renderOpenGL)
    // exactly like parameterFloatValues/parameterColorValues above: one
    // glBindTexture + one sampler uniform per entry, every frame.
    std::unordered_map<std::string, std::shared_ptr<gl::Texture2D>> textureBindings;

    juce::OpenGLShaderProgram* Resolve(ShaderComposer& composer, const juce::OpenGLContext& context);

    // `tint` is a per-instance multiplier (identity {1,1,1} by default),
    // never baked into `albedo` itself -- `albedo` is this shared asset's
    // own authored color (several entities can point at the same
    // Material, per MeshRenderer's own comment), while `tint` is where a
    // runtime graph or host capability updates (see ce::engine::Tint).
    // Multiplying at the uniform boundary keeps the shared asset
    // untouched no matter how many tinted instances read it.
    void ApplyUniforms(juce::OpenGLShaderProgram& program, juce::Vector3D<float> tint = { 1.0f, 1.0f, 1.0f }) const;

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
    juce::String cachedMaterialSource_;
    bool hasResolvedOnce_ = false;
};

} // namespace ce
