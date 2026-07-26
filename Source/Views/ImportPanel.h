#pragma once

#include <JuceHeader.h>

#include "Audio/AudioCatalog.h"
#include "Audio/AudioPreviewPlayer.h"
#include "engine/world.h"
#include "Import/ImporterRegistry.h"
#include "Render/ViewportComponent.h"

namespace ce {

// AI1/AI2: the Import Hub shell. Accepts files dragged in from the OS
// (juce::FileDragAndDropTarget), looks up the right ce::import::
// AssetImporter for each by extension via registry_, and runs it. This
// milestone deliberately stops at "drop a file, see whether it worked"
// (plus, for audio, a Play/Stop preview) -- per-format configuration
// (bone retargeting, timeline slicing, audio resampling, ...) is its own
// later milestone once there's an actual data model (skeletons, clips)
// worth configuring; building that UI now would just be decoration over
// nothing.
//
// Owns the ImporterRegistry itself (RegisterBuiltins() in the
// constructor) -- MainComponent doesn't need to know what formats exist,
// only that this panel handles "Assets". Also owns the AudioCatalog and
// AudioPreviewPlayer imported clips land in and play back from -- there's
// no ECS "play this in the scene" concept yet for audio to plug into
// (unlike 3D assets, which already have AssetCatalog + the "+ Add" menu),
// so for now the catalog only exists to make imported clips audible here.
class ImportPanel final : public juce::Component,
                           public juce::FileDragAndDropTarget {
public:
    ImportPanel(engine::World& world, ViewportComponent& viewport);

    // Declared (not defaulted inline) and defined in the .cpp, after
    // AudioClipRow's full definition: audioClipRows_ is a
    // juce::OwnedArray<AudioClipRow> where AudioClipRow is only forward-
    // declared here, so an implicit/inline destructor would need
    // AudioClipRow complete wherever ImportPanel is destroyed -- including
    // MainComponent.cpp, which only ever sees this forward declaration.
    ~ImportPanel() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class AudioClipRow;

    void AppendLogLine(const juce::String& line);
    void RebuildAudioClipRows();

    // Only one preview clip can play at a time (previewPlayer_ is a
    // single player) -- TogglePlay/IsPlayingClip/RefreshPlayButtonLabels
    // keep every row's button text in sync with that single shared piece
    // of state, e.g. so clicking Play on clip B flips clip A's button
    // (if A was playing) back to "Play" too.
    void TogglePlay(const juce::String& name);
    bool IsPlayingClip(const juce::String& name) const { return currentlyPlayingName_ == name; }
    void RefreshPlayButtonLabels();

    import::ImporterRegistry registry_;
    import::ImportContext context_;

    juce::AudioFormatManager audioFormatManager_;
    audio::AudioCatalog audioCatalog_;
    audio::AudioPreviewPlayer previewPlayer_;
    juce::String currentlyPlayingName_;

    juce::Label titleLabel_{ {}, "Import" };
    juce::Label dropZoneLabel_{ {},
                                 "Drag files here to import\n(currently: glTF/GLB models, WAV/AIFF/FLAC audio, "
                                 "PNG/JPG/TGA/BMP/HDR textures)" };
    juce::TextEditor log_;

    juce::Label audioClipsLabel_{ {}, "Audio Clips" };
    juce::OwnedArray<AudioClipRow> audioClipRows_;

    bool isDragHovering_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportPanel)
};

} // namespace ce
