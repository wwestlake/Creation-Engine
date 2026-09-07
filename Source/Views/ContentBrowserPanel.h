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
// Editor UI/Workflow Overhaul plan, Phase 4: one flat, alphabetically-
// sorted list -- NOT grouped by AssetKind. AssetKind
// (shared/AssetSystem/include/creation/assets/AssetTypes.h) is a Suite-wide
// enum mixing audio/music-production kinds (patch, foleyPatch,
// trackerArrangement, samplePack, midi) in with the four Creation Engine
// actually has (pod, objectDefinition, game, scene); surfacing it as
// user-facing grouping was engine-internal plumbing leaking into the UI,
// confirmed directly as a real discoverability failure. Creating anything
// now goes through the one visible "Create v" menu under the title
// (Decision 2) instead of a hidden right-click-only affordance on a
// section header. A row's actions (Delete/Export/Reimport/Rename where
// applicable) live behind right-click on that row instead of always-
// visible per-row buttons. A kind editor (the Pod editor) is still never a
// standing dock tab of its own; this panel still owns discovery and
// creation.
class ContentBrowserPanel final : public juce::Component,
                                  public juce::FileDragAndDropTarget {
public:
    ContentBrowserPanel(ViewportComponent& viewport, ImportPanel& importPanel, frust::PodCatalog& podCatalog,
                        scene::ObjectDefinitionCatalog& objectDefinitions);
    ~ContentBrowserPanel() override;

    // Dragging a file onto this panel imports it -- the same underlying
    // pipeline the old standalone Import screen used (importPanel_ still
    // owns that logic; it's just never shown as its own panel anymore).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

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

    // Fired instead of the generic per-version delete (PerformDelete) when
    // the row being deleted is a Game or a Scene -- deleting either needs
    // the games.xml cascade EngineGameDocumentStore::deleteGame/deleteScene
    // provides, not a bare asset-descriptor removal. MainComponent wires
    // these to that store and refreshes games_/activeGame_/activeScene_
    // afterward.
    std::function<void(juce::String catalogAssetId)> onGameDeleteRequested;
    std::function<void(juce::String catalogAssetId)> onSceneDeleteRequested;
    // Same shape, for the row context menu's Rename action -- only offered
    // for Game/Scene rows (the only kinds with any rename capability
    // today; MainComponent's RenameGame/RenameScene already existed,
    // previously only reachable from the now-deleted Explorer panel).
    std::function<void(juce::String catalogAssetId)> onGameRenameRequested;
    std::function<void(juce::String catalogAssetId)> onSceneRenameRequested;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Editor UI/Workflow Overhaul plan, Phase 5: public (not just the
    // Create menu's own private call site) so MainComponent can reuse this
    // exact creation logic for the Properties panel's object-first "Open
    // Editor" flow -- create a Pod, then attach it to the selected entity
    // immediately, so the code is connected from the instant it exists.
    // Returns the new Pod's name, or an empty string on failure (already
    // reported to the user via an AlertWindow).
    juce::String CreateNewPod(frust::PodKind kind);

private:
    class AssetRow;
    void OpenAsset(const creation::assets::AssetDescriptor& descriptor);
    void CreateNewObjectDefinition();
    // The one visible entry point for creating anything (Decision 2) --
    // shows the "Create v" menu's items. Extensible: a future asset kind
    // is a one-line addition here, matching StarterGameTemplates.h's own
    // open-ended-vector precedent.
    void ShowCreateMenu();
    // Right-click on a row -- Delete/Export/Reimport always offered;
    // Rename only for Game/Scene (the only kinds with any rename
    // capability today).
    void ShowRowContextMenu(const creation::assets::AssetDescriptor& descriptor);
    void RenameAsset(const creation::assets::AssetDescriptor& descriptor);

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

    // Writes one asset's stored content back out to a file the user picks
    // -- the read half of the durable VFS entry this row's descriptor
    // points at (logicalPath), materialized to plain disk bytes. Doesn't
    // try to reconstruct sibling files (textures, .bin buffers) that a
    // model's import may have preserved alongside it -- that's real, but
    // separate, follow-on scope.
    void ExportAsset(const creation::assets::AssetDescriptor& latest);

    std::unique_ptr<juce::FileChooser> activeRelocateChooser_; // kept alive for the duration of one async pick.
    std::unique_ptr<juce::FileChooser> activeExportChooser_; // same, for ExportAsset's Save As dialog.

    ViewportComponent& viewport_;
    ImportPanel& importPanel_;
    frust::PodCatalog& podCatalog_;
    scene::ObjectDefinitionCatalog& objectDefinitions_;
    creation::assets::ProjectSession* projectSession_ = nullptr;

    juce::Label titleLabel_{ {}, "Content Browser" };
    juce::TextButton createMenuButton_{ "Create \xe2\x96\xbe" }; // trailing UTF-8 down-chevron.
    juce::TextButton importButton_{ "Import..." };
    juce::Label hintLabel_{ {}, "Drag a placeable asset into the viewport to add it to the scene. Right-click an asset for more actions." };
    juce::TextEditor searchBox_;
    juce::TextButton searchButton_{ "\xf0\x9f\x94\x8d" }; // magnifying glass.
    juce::Label emptyLabel_{ {}, "Open a project to browse its assets." };

    // Rows live inside this plain host, not directly on `this` --
    // scrollView_ scrolls the host, so the row list can grow past the
    // panel's own (often short, docked-at-the-bottom) height without
    // clipping.
    juce::Viewport scrollView_;
    juce::Component rowsHost_;
    juce::OwnedArray<AssetRow> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentBrowserPanel)
};

} // namespace ce::views
