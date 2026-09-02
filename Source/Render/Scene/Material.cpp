#include "Render/Scene/Material.h"

namespace ce {

juce::OpenGLShaderProgram* Material::Resolve(ShaderComposer& composer, const juce::OpenGLContext& context) {
    if (compiledMaterialSource.isNotEmpty()) {
        if (hasResolvedOnce_ && compiledMaterialSource == cachedMaterialSource_) {
            return cachedProgram_;
        }
        cachedProgram_ = composer.GetProgram(context, "programs/material_host.vert", "programs/material_host.frag",
                                             {}, compiledMaterialSource);
        cachedMaterialSource_ = compiledMaterialSource;
        hasResolvedOnce_ = true;
        return cachedProgram_;
    }

    const bool hasTexture = albedoTexture != nullptr && albedoTexture->IsValid();

    if (hasResolvedOnce_ && cachedMaterialSource_.isEmpty() && hasTexture == cachedHadTexture_ && isSkinned == cachedWasSkinned_) {
        return cachedProgram_;
    }

    std::vector<juce::String> defines;
    if (hasTexture) {
        defines.push_back("USE_ALBEDO_TEXTURE");
    }
    if (isSkinned) {
        defines.push_back("USE_SKINNING");
    }

    cachedProgram_ = composer.GetProgram(context, vertexEntry, fragmentEntry, defines);
    cachedHadTexture_ = hasTexture;
    cachedWasSkinned_ = isSkinned;
    cachedMaterialSource_.clear();
    hasResolvedOnce_ = true;
    return cachedProgram_;
}

void Material::ApplyUniforms(juce::OpenGLShaderProgram& program, juce::Vector3D<float> tint) const {
    // A compiled material graph's EvaluateMaterial computes
    // albedo/metallic/roughness internally -- nothing to set here for
    // that path (its own parameter/texture uniforms, once graphs use
    // them, are a Stage 2 concern -- see docs/MATERIAL_SYSTEM_PLAN.md).
    if (compiledMaterialSource.isNotEmpty()) {
        return;
    }

    program.setUniform("uAlbedo", albedo.x * tint.x, albedo.y * tint.y, albedo.z * tint.z);
    program.setUniform("uMetallic", metallic);
    program.setUniform("uRoughness", roughness);

    if (albedoTexture != nullptr && albedoTexture->IsValid()) {
        albedoTexture->Bind(0);
        program.setUniform("uAlbedoTexture", 0);
    }
}

} // namespace ce
