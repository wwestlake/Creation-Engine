#include "Render/Shaders/ShaderComposer.h"

#include <algorithm>
#include <iostream>

namespace ce {

ShaderComposer::ShaderComposer(juce::File shaderRoot) : shaderRoot_(std::move(shaderRoot)) {}

ShaderComposer::ShaderComposer(creation::assets::VirtualFileSystem& vfs, juce::String vfsRootPrefix)
    : vfs_(&vfs), vfsRootPrefix_(std::move(vfsRootPrefix)) {}

bool ShaderComposer::ReadTextByRelativePath(const juce::String& relativePath, juce::String& outText) const {
    if (vfs_ != nullptr) {
        juce::MemoryBlock data;
        if (!vfs_->readFile(vfsRootPrefix_ + relativePath, data)) {
            return false;
        }
        outText = juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize()));
        return true;
    }

    const juce::File file = shaderRoot_.getChildFile(relativePath);
    if (!file.existsAsFile()) {
        return false;
    }
    outText = file.loadFileAsString();
    return true;
}

juce::String ShaderComposer::ResolveIncludes(const juce::String& source, std::vector<juce::String>& alreadyIncluded) {
    juce::StringArray lines = juce::StringArray::fromLines(source);
    juce::String result;

    for (const auto& line : lines) {
        const auto trimmed = line.trim();

        if (!trimmed.startsWith("#include")) {
            result << line << "\n";
            continue;
        }

        const auto firstQuote = trimmed.indexOfChar('"');
        const auto lastQuote = trimmed.lastIndexOfChar('"');
        if (firstQuote < 0 || lastQuote <= firstQuote) {
            std::cout << "[shader] ERROR: malformed #include directive: " << line << std::endl;
            continue;
        }

        const auto includeRelative = trimmed.substring(firstQuote + 1, lastQuote);

        if (std::find(alreadyIncluded.begin(), alreadyIncluded.end(), includeRelative) != alreadyIncluded.end()) {
            continue; // already inlined elsewhere in this composition — skip, like #pragma once.
        }
        alreadyIncluded.push_back(includeRelative);

        juce::String includedSource;
        if (!ReadTextByRelativePath(includeRelative, includedSource)) {
            std::cout << "[shader] ERROR: include not found: " << includeRelative << std::endl;
            continue;
        }

        result << ResolveIncludes(includedSource, alreadyIncluded) << "\n";
    }

    return result;
}

juce::String ShaderComposer::ComposeSource(const juce::String& entryRelativePath,
                                            const std::vector<juce::String>& defines) {
    juce::String raw;
    if (!ReadTextByRelativePath(entryRelativePath, raw)) {
        std::cout << "[shader] ERROR: entry file not found: " << entryRelativePath << std::endl;
        return {};
    }

    juce::StringArray lines = juce::StringArray::fromLines(raw);
    if (lines.isEmpty() || !lines[0].trim().startsWith("#version")) {
        std::cout << "[shader] ERROR: " << entryRelativePath << " must start with a #version line" << std::endl;
        return {};
    }

    const juce::String versionLine = lines[0];
    lines.remove(0);
    const juce::String rest = lines.joinIntoString("\n");

    std::vector<juce::String> alreadyIncluded{ entryRelativePath };
    const juce::String resolvedRest = ResolveIncludes(rest, alreadyIncluded);

    juce::String defineBlock;
    for (const auto& define : defines) {
        defineBlock << "#define " << define << "\n";
    }

    return versionLine + "\n" + defineBlock + resolvedRest;
}

juce::OpenGLShaderProgram* ShaderComposer::GetProgram(const juce::OpenGLContext& context,
                                                        const juce::String& vertexEntry,
                                                        const juce::String& fragmentEntry,
                                                        const std::vector<juce::String>& defines) {
    std::vector<juce::String> sortedDefines = defines;
    std::sort(sortedDefines.begin(), sortedDefines.end());

    juce::String keyString = vertexEntry + "|" + fragmentEntry;
    for (const auto& define : sortedDefines) {
        keyString << "|" << define;
    }
    const std::string key = keyString.toStdString();

    if (auto it = cache_.find(key); it != cache_.end()) {
        std::cout << "[shader] cache hit: " << key << std::endl;
        return it->second.get();
    }

    std::cout << "[shader] compiling: " << key << std::endl;

    const juce::String vertexSource = ComposeSource(vertexEntry, sortedDefines);
    const juce::String fragmentSource = ComposeSource(fragmentEntry, sortedDefines);
    if (vertexSource.isEmpty() || fragmentSource.isEmpty()) {
        std::cout << "[shader] ERROR: source composition failed for " << key << std::endl;
        return nullptr;
    }

    auto program = std::make_unique<juce::OpenGLShaderProgram>(context);
    if (!program->addVertexShader(vertexSource) || !program->addFragmentShader(fragmentSource) ||
        !program->link()) {
        std::cout << "[shader] ERROR compiling " << key << ": " << program->getLastError() << std::endl;
        return nullptr;
    }

    auto* rawPtr = program.get();
    cache_.emplace(key, std::move(program));
    return rawPtr;
}

} // namespace ce
