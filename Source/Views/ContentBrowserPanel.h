#pragma once

#include <JuceHeader.h>

#include <creation/assets/ProjectSession.h>

#include "Frust/PodCatalog.h"
#include "Scene/ObjectDefinitions.h"
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
//
// Assets are grouped into a collapsible section per AssetKind (a Pods
// section, a Model section, etc.) rather than one flat sorted list -- this
// IS the asset tool the engine is missing: browse what exists, right-click
// a kind's section to create a new one of that kind. The Pods section
// always renders, even with zero Pods, so there's always somewhere to
// right-click "New Pod" -- other kinds only get a section once they have
// at least one asset (they're populated via Import, unchanged, no "New"
// affordance needed for them yet). A kind editor (the Pod editor) is never
// a standing dock tab of its own; this panel is what owns discovery and
// creation, matching Unreal Engine 4's actual Content Browser workflow.
// Pod/Asset Workflow plan, Phase 3.
class ContentBrowserPanel final : public juce::Component {
public:
    ContentBrowserPanel(ViewportComponent& viewport, ImportPanel& importPanel, frust::PodCatalog& podCatalog,
                        scene::ObjectDefinitionCatalog& objectDefinitions);
    ~ContentBrowserPanel() override;

    void SetProjectContent(creation::assets::ProjectSession* session);
    void Refresh();

    // Fired when a row is clicked (not one of its buttons) -- built
    // generically on the clicked descriptor so MainComponent can decide
    // what "open" means per AssetKind (today: AssetKind::pod opens the
    // Pod editor); wiring another kind's open-behavior later is additive
    // here, not a rewrite.
    std::function<void(creation::assets::AssetDescriptor)> onAssetOpened;

    // Fired after a new Pod is created (and already saved/persisted) via
    // the Pods section's right-click menu -- MainComponent opens it in the
    // Pod editor, registering that editor's dock panels on demand if
    // they're not already open.
    std::function<void(juce::String)> onPodCreated;

    // Same idea, for a newly created Object Definition -- MainComponent
    // opens it in the (much smaller) Object Definition editor.
    std::function<void(juce::String)> onObjectDefinitionCreated;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class AssetRow;
    class Section;
    void OpenAsset(const creation::assets::AssetDescriptor& descriptor);
    void CreateNewPod(frust::PodKind kind);
    void CreateNewObjectDefinition();
    Section* FindOrCreateSection(creation::assets::AssetKind kind);

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
    frust::PodCatalog& podCatalog_;
    scene::ObjectDefinitionCatalog& objectDefinitions_;
    creation::assets::ProjectSession* projectSession_ = nullptr;

    juce::Label titleLabel_{ {}, "Content Browser" };
    juce::Label hintLabel_{ {}, "This project's assets, grouped by kind." };
    juce::TextEditor searchBox_;
    juce::Label emptyLabel_{ {}, "Open a project to browse its assets." };

    juce::OwnedArray<Section> sections_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentBrowserPanel)
};

} // namespace ce::views
