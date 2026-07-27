#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>

namespace ce::scene {

// One staged CEL script asset -- just its source text. Deliberately not
// compiled at import time: compilation needs a live ce::engine::
// IScriptRuntime (see ScriptPanel), and GS7's own scope is background,
// on-demand compilation from the editor panel, not at drag-drop time.
// A `.cel` file with a syntax/semantic error still stages successfully
// here -- ScriptPanel's Compile button is where that error actually
// surfaces, exactly like a designer would encounter it when attaching
// and compiling the script against a real entity.
struct ScriptAsset {
    juce::String name;
    juce::String source;
};

// Named catalog of staged CEL script sources, analogous to
// scene::AssetCatalog (meshes/materials) and audio::AudioCatalog (audio
// clips) -- no GL context involved (like AudioCatalog), so no
// render-thread hop is needed to add one. Lives on ViewportComponent
// rather than ImportPanel (unlike AudioCatalog) because, like
// AssetCatalog, entities need to reference it directly (ScriptPanel's
// "Attach Script" combo) rather than being self-contained within the
// Import Hub's own preview UI.
//
// mutex_ guards assets_/names_ for the same reason AssetCatalog's and
// AudioCatalog's do: AddFromFile() runs on the message thread (via the
// Import Hub) and can race a concurrent Find()/Names() from ScriptPanel.
// Find() returns by value for the same reason those two do -- a
// concurrent re-import of the same name must not mutate a ScriptAsset a
// caller elsewhere is still reading through a reference to.
class ScriptCatalog final {
public:
    // Stages `file`'s full text content under `name`, overwriting any
    // existing asset with that name. Returns false only if the file
    // can't be read at all (permissions, disappeared mid-drop, ...) --
    // a script with invalid CEL syntax is still staged successfully;
    // see ScriptAsset's own comment.
    bool AddFromFile(const juce::String& name, const juce::File& file);

    ScriptAsset Find(const juce::String& name) const;
    std::vector<juce::String> Names() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ScriptAsset> assets_;
    std::vector<juce::String> names_;
};

} // namespace ce::scene
