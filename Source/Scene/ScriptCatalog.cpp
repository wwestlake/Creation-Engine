#include "Scene/ScriptCatalog.h"

#include <iostream>

namespace ce::scene {

bool ScriptCatalog::AddFromFile(const juce::String& name, const juce::File& file) {
    if (!file.existsAsFile()) {
        std::cout << "[script] failed to open " << file.getFullPathName() << " -- file does not exist." << std::endl;
        return false;
    }

    ScriptAsset asset;
    asset.name = name;
    asset.source = file.loadFileAsString();

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (assets_.find(name.toStdString()) == assets_.end()) {
            names_.push_back(name);
        }
        assets_[name.toStdString()] = std::move(asset);
    }

    std::cout << "[script] staged " << file.getFileName() << std::endl;
    return true;
}

ScriptAsset ScriptCatalog::Find(const juce::String& name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = assets_.find(name.toStdString());
    return it == assets_.end() ? ScriptAsset{} : it->second;
}

std::vector<juce::String> ScriptCatalog::Names() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return names_;
}

} // namespace ce::scene
