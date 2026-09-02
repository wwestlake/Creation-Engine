#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <JuceHeader.h>

namespace ce::audio {

// One decoded audio clip. Fully decoded into memory rather than streamed
// from disk -- reasonable for the short SFX/UI clips this importer
// targets; a future streaming path for long music tracks is a real but
// separate concern, not built here since nothing has asked for it yet.
struct AudioAsset {
    juce::String name;
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    double sampleRate = 44100.0;
    double lengthSeconds = 0.0;

    // 2D (UI/music, no positioning) vs 3D (spatialized in-scene sound).
    // Recorded here per the import spec's request but not yet consumed by
    // anything -- there's no in-scene audio playback system yet for it to
    // configure. Defaults to 2D, the safer/simpler assumption for a
    // freshly-imported clip.
    bool is3D = false;
};

// Named catalog of decoded audio clips, analogous to scene::AssetCatalog
// but for audio -- no GL context involved, so (unlike AssetCatalog)
// there's no render-thread hop needed to add one.
//
// mutex_ guards assets_/names_ for the same reason AssetCatalog's does:
// AddFromFile() can be called (from the message thread, via the Import
// Hub) at any time after startup, potentially while something else reads
// Find()/Names() concurrently. Find() returns by value (a couple of cheap
// shared_ptr copies) rather than a reference into the map for the same
// reason AssetCatalog::Find() does -- a concurrent re-import of the same
// name must not be able to mutate an Asset a caller elsewhere is still
// reading through a pointer to.
class AudioCatalog final {
public:
    // Decodes file via formatManager and registers it under `name`,
    // overwriting any existing asset with that name. Returns false (and
    // logs why) if the file can't be opened/decoded by any registered
    // format.
    bool AddFromFile(const juce::String& name, const juce::File& file, juce::AudioFormatManager& formatManager);

    AudioAsset Find(const juce::String& name) const;
    std::vector<juce::String> Names() const;

    // Evicts a clip from the catalog. Same "runtime cache only, no
    // dependency check, unconditional" contract as scene::AssetCatalog::
    // Remove() -- a caller currently previewing this clip through
    // AudioPreviewPlayer is responsible for stopping playback first if it
    // wants to, this doesn't do it automatically. Returns false if name
    // isn't a registered clip.
    bool Remove(const juce::String& name);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, AudioAsset> assets_;
    std::vector<juce::String> names_;
};

} // namespace ce::audio
