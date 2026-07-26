#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>

namespace ce {

// Assembles GLSL programs from a library of reusable text chunks before
// handing the result to juce::OpenGLShaderProgram — the "shader language"
// this engine uses is plain GLSL plus this composition layer, not a new
// grammar (capabilities-plan decision, M2).
//
// Entry files and #include targets are paths relative to the shader root
// (CE_SHADER_SOURCE_DIR, pointing straight at Source/Render/Shaders for
// this dev build — no copy/packaging step yet, so editing a .glsl file
// on disk is picked up on the next GetProgram() call for that variant).
//
// Composition rules:
//   - The entry file's first line must be `#version ...` and stays first.
//   - `#include "path/from/shader/root.glsl"` lines are resolved
//     recursively, always relative to shaderRoot_ — the same include path
//     works no matter which file it's written in, so chunks don't have to
//     know how deeply nested their includer is. Each resolved path is
//     only ever inlined once per composed source (a chunk defining
//     functions would otherwise redefine them on a second include).
//   - `defines` are injected as `#define KEY VALUE` lines immediately
//     after the version line, before any included content.
class ShaderComposer final {
public:
    explicit ShaderComposer(juce::File shaderRoot);

    // vertexEntry/fragmentEntry: paths relative to shaderRoot, e.g.
    // "programs/pbr_lit.vert" / "programs/pbr_lit.frag". Returns a linked
    // program from the cache (compiling/linking + logging on a miss), or
    // nullptr if compilation/linking failed (also logged).
    juce::OpenGLShaderProgram* GetProgram(const juce::OpenGLContext& context, const juce::String& vertexEntry,
                                           const juce::String& fragmentEntry,
                                           const std::vector<juce::String>& defines = {});

private:
    juce::String ComposeSource(const juce::String& entryRelativePath, const std::vector<juce::String>& defines);
    juce::String ResolveIncludes(const juce::String& source, std::vector<juce::String>& alreadyIncluded);
    juce::String ReadFile(const juce::File& file);

    juce::File shaderRoot_;
    std::unordered_map<std::string, std::unique_ptr<juce::OpenGLShaderProgram>> cache_;
};

} // namespace ce
