#pragma once

#include <JuceHeader.h>

#include <creation/assets/ProjectSession.h>

#include "Views/ImportPanel.h"

namespace ce {
class ViewportComponent;
}

namespace ce::views {

// Tool-local content browser for the current project's persisted assets
// (models, textures, audio -- see docs/architecture/Suite-Asset-Pipeline-
// Model.md). Distinct from any future suite-wide asset browser: this only
// ever shows what SetProjectContent's ProjectSession has, i.e. the open
// project's own assets.
//
// Reads straight from ProjectSession's AssetCatalog (the durable, shared-
// VFS-backed record) rather than the Engine-local runtime AssetCatalog/
// AudioCatalog (viewport_.Catalog() / importPanel_.GetAudioCatalog()) --
// those are GPU/decode caches, not sources of truth for "what assets does
// this project have." Delete has to touch both: the durable copy (so it
// stays gone after reopening the project) and the runtime cache (so it
// stops being placeable/audible immediately, without waiting for a
// restart) -- see DeleteAsset().
//
// Materials are NOT shown here yet -- MaterialGraphPanel's compileAndSave
// only ever writes into the runtime catalog, never into ProjectSession, so
// there is no durable Material asset for this panel to find. That's a
// separate, already-identified piece of pending work (the Materials-editor
// redesign), not an oversight here.
class ContentBrowserPanel final : public juce::Component {
public:
    ContentBrowserPanel(ViewportComponent& viewport, ImportPanel& importPanel);
    ~ContentBrowserPanel() override;

    void SetProjectContent(creation::assets::ProjectSession* session);
    void Refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class AssetRow;

    // Runs the delete-with-dependency-check operation (Suite-Asset-
    // Pipeline-Model.md) for one logical asset: findDependents() first: if
    // anyone depends on it, warn and require an explicit "Delete Anyway"
    // rather than proceeding straight to the plain confirm. Either path
    // ends the same way -- every version's entry+descriptor removed from
    // the project, then the corresponding runtime cache evicted by kind.
    void DeleteAsset(const creation::assets::AssetDescriptor& latest);
    void PerformDelete(const creation::assets::AssetDescriptor& latest);

    // Reimport / Update (Suite-Asset-Pipeline-Model.md, Phase 4). Reads
    // latest.externalSourcePath; if that file's missing, offers a native
    // file-browse dialog to relocate it rather than dead-ending -- either
    // way ends at RunReimport with a real, existing file.
    void ReimportAsset(const creation::assets::AssetDescriptor& latest);
    void RunReimport(const creation::assets::AssetDescriptor& latest, const juce::File& sourceFile);

    std::unique_ptr<juce::FileChooser> activeRelocateChooser_; // kept alive for the duration of one async pick.

    ViewportComponent& viewport_;
    ImportPanel& importPanel_;
    creation::assets::ProjectSession* projectSession_ = nullptr;

    juce::Label titleLabel_{ {}, "Content Browser" };
    juce::Label hintLabel_{ {}, "This project's imported models, textures, and audio." };
    juce::TextEditor searchBox_;
    juce::Label emptyLabel_{ {}, "No assets imported into this project yet -- see Assets & Import." };

    juce::OwnedArray<AssetRow> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentBrowserPanel)
};

} // namespace ce::views
