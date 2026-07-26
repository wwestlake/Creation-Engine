#pragma once

#include <JuceHeader.h>

#include "engine/world.h"
#include "Import/ImporterRegistry.h"
#include "Render/ViewportComponent.h"

namespace ce {

// AI1: the Import Hub shell. Accepts files dragged in from the OS
// (juce::FileDragAndDropTarget), looks up the right ce::import::
// AssetImporter for each by extension via registry_, and runs it. This
// milestone deliberately stops at "drop a file, see whether it worked" --
// per-format configuration (bone retargeting, timeline slicing, audio
// resampling, ...) is its own later milestone once there's an actual
// data model (skeletons, clips) worth configuring; building that UI now
// would just be decoration over nothing.
//
// Owns the ImporterRegistry itself (RegisterBuiltins() in the
// constructor) -- MainComponent doesn't need to know what formats exist,
// only that this panel handles "Assets".
class ImportPanel final : public juce::Component,
                           public juce::FileDragAndDropTarget {
public:
    ImportPanel(engine::World& world, ViewportComponent& viewport);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void AppendLogLine(const juce::String& line);

    import::ImporterRegistry registry_;
    import::ImportContext context_;

    juce::Label titleLabel_{ {}, "Import" };
    juce::Label dropZoneLabel_{ {}, "Drag files here to import\n(currently: glTF/GLB models)" };
    juce::TextEditor log_;

    bool isDragHovering_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportPanel)
};

} // namespace ce
