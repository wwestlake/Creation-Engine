#pragma once

#include <JuceHeader.h>

#include "Audio/AudioCatalog.h"
#include "Audio/AudioPreviewPlayer.h"
#include "engine/world.h"
#include "Import/ImporterRegistry.h"
#include "Render/ViewportComponent.h"
#include <creation/assets/ProjectSession.h>

namespace ce {

// AI1/AI2: the Import Hub shell. Accepts files dragged in from the OS
// (juce::FileDragAndDropTarget), looks up the right ce::import::
// AssetImporter for each by extension via registry_, and runs it, plus
// (for audio) a Play/Stop preview.
//
// AI6 added an "Animation Import Options" section: interpolation
// override, root motion extraction, and timeline slicing -- these are
// configured here BEFORE dropping a file (not a post-drop dialog; this
// codebase has no modal-dialog convention anywhere else, so "set the
// options, then drop" matches how every other control in this app
// already works) and read once per filesDropped() call into
// context_.animationOptions. GltfAssetImporter is the only importer that
// looks at that field; a plain audio/texture drop ignores it entirely.
// Bone retargeting (source->target joint name mapping) is explicitly NOT
// here -- there's no target-skeleton concept to retarget onto yet (every
// imported asset owns its own independent skeleton), so a mapping table
// would have nothing real to configure. Worth revisiting once clips can
// actually be shared across differently-named skeletons.
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
    ImportPanel(engine::World& world, ViewportComponent& viewport, creation::assets::ProjectSession& projectSession);

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
    class SliceRow;

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

    // AI6
    import::AnimationImportOptions BuildAnimationImportOptions() const;
    void AddSliceRow();
    void RemoveSliceRow(SliceRow* row);

    import::ImporterRegistry registry_;
    import::ImportContext context_;

    juce::AudioFormatManager audioFormatManager_;
    audio::AudioCatalog audioCatalog_;
    audio::AudioPreviewPlayer previewPlayer_;
    juce::String currentlyPlayingName_;

    juce::Label titleLabel_{ {}, "Import" };
    juce::Label dropZoneLabel_{ {},
                                 "Drag files here to import\n(currently: glTF/GLB/OBJ models, WAV/AIFF/FLAC audio, "
                                 "PNG/JPG/TGA/BMP/HDR textures)" };
    juce::TextEditor log_;

    juce::Label audioClipsLabel_{ {}, "Audio Clips" };
    juce::OwnedArray<AudioClipRow> audioClipRows_;

    // AI6: read into context_.animationOptions at the top of each
    // filesDropped() call -- see this class's header comment.
    juce::Label animationOptionsLabel_{ {}, "Animation Import Options" };
    juce::Label interpolationLabel_{ {}, "Interpolation" };
    juce::ComboBox interpolationCombo_;
    juce::ToggleButton rootMotionToggle_{ "Extract root motion (root joint)" };
    juce::Label slicesLabel_{ {}, "Timeline Slices (optional)" };
    juce::OwnedArray<SliceRow> sliceRows_;
    juce::TextButton addSliceButton_{ "+ Add Slice" };

    bool isDragHovering_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportPanel)
};

} // namespace ce
