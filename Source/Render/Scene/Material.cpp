#include "Render/Scene/Material.h"

namespace ce {

juce::OpenGLShaderProgram* Material::Resolve(ShaderComposer& composer, const juce::OpenGLContext& context) {
    const bool hasTexture = albedoTexture != nullptr && albedoTexture->IsValid();

    if (hasResolvedOnce_ && hasTexture == cachedHadTexture_) {
        return cachedProgram_;
    }

    std::vector<juce::String> defines;
    if (hasTexture) {
        defines.push_back("USE_ALBEDO_TEXTURE");
    }

    cachedProgram_ = composer.GetProgram(context, vertexEntry, fragmentEntry, defines);
    cachedHadTexture_ = hasTexture;
    hasResolvedOnce_ = true;
    return cachedProgram_;
}

void Material::ApplyUniforms(juce::OpenGLShaderProgram& program) const {
    program.setUniform("uAlbedo", albedo.x, albedo.y, albedo.z);
    program.setUniform("uMetallic", metallic);
    program.setUniform("uRoughness", roughness);

    if (albedoTexture != nullptr && albedoTexture->IsValid()) {
        albedoTexture->Bind(0);
        program.setUniform("uAlbedoTexture", 0);
    }
}

} // namespace ce
