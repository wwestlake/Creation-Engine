#include "Views/ContentBrowserPanel.h"

#include <algorithm>
#include <unordered_map>

#include <creation/assets/ProjectAssetService.h>

#include "Import/AssetImporter.h"
#include "Import/ImporterRegistry.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"

namespace ce::views {

namespace {
juce::String KindLabel(frust::PodKind kind) { return kind == frust::PodKind::Processing ? "Processing" : "Behavior"; }

// Only kinds that make sense as a standalone scene entity are drag-
// placeable -- a Pod attaches TO something rather than existing on its
// own, Audio has no in-scene representation yet, etc. Render (a raw
// model) and Object Definition (mesh + materials + Pods, the reusable
// "thing" recipe) are the only two today.
bool IsPlaceableKind(creation::assets::AssetKind kind) {
    return kind == creation::assets::AssetKind::render || kind == creation::assets::AssetKind::objectDefinition;
}

juce::String FormatFileSize(std::int64_t bytes) {
    if (bytes < 1024) return juce::String(bytes) + " B";
    if (bytes < 1024 * 1024) return juce::String(bytes / 1024.0, 1) + " KB";
    return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
}

// Kind-only creation (no name box, ever) needs a default name generated
// for it -- "New Behavior Pod", then "New Behavior Pod 2", etc., the
// first-free-numbered-slot shape most editors use for untitled documents.
juce::String GenerateDefaultPodName(frust::PodCatalog& catalog, frust::PodKind kind) {
    const juce::String base = "New " + KindLabel(kind) + " Pod";
    const auto existing = catalog.Names(kind);
    auto isTaken = [&](const juce::String& candidate) {
        return std::any_of(existing.begin(), existing.end(),
                            [&](const juce::String& name) { return name.equalsIgnoreCase(candidate); });
    };
    if (!isTaken(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        const auto candidate = base + " " + juce::String(i);
        if (!isTaken(candidate)) return candidate;
    }
    return base + " " + juce::String(juce::Time::currentTimeMillis());
}

// Same shape as GenerateDefaultPodName above -- kind-only creation, no
// name box, so a default identifier is generated instead.
juce::String GenerateDefaultObjectDefinitionName(scene::ObjectDefinitionCatalog& catalog) {
    const juce::String base = "New Object Definition";
    const auto existing = catalog.ids();
    auto isTaken = [&](const juce::String& candidate) {
        return std::any_of(existing.begin(), existing.end(),
                            [&](const juce::String& id) { return id.equalsIgnoreCase(candidate); });
    };
    if (!isTaken(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        const auto candidate = base + " " + juce::String(i);
        if (!isTaken(candidate)) return candidate;
    }
    return base + " " + juce::String(juce::Time::currentTimeMillis());
}
}

// One row: the durable project asset's display name/kind/size/modified
// time. Deliberately shows only the LATEST version of each logical asset
// -- older reimport history exists (AssetCatalog::findAllVersions) but
// isn't browsable UI yet, same "not every real field needs a row today"
// scoping ImportPanel's own AI6 section applied to animation options.
//
// Editor UI/Workflow Overhaul plan, Phase 4: no more standing Delete/
// Export/Reimport buttons -- those are right-click actions now
// (ShowContextMenu below), since a per-row button strip was confirmed as
// a real complaint ("that is not how it is supposed to work... an old
// web-days pattern"). kindLabel_ takes the slot the old categoryLabel_
// had -- with no per-kind section header anymore, SOME per-row kind
// indication is still needed so Pods/Object Definitions/Games/Scenes
// don't all visually blend together in one flat list.
class ContentBrowserPanel::AssetRow final : public juce::Component,
                                            public juce::SettableTooltipClient {
public:
    AssetRow(ContentBrowserPanel& owner, creation::assets::AssetDescriptor descriptor)
        : owner_(owner), descriptor_(std::move(descriptor)), placeable_(IsPlaceableKind(descriptor_.kind)) {
        // Every plain label below opts out of its own mouse handling --
        // juce::Label claims clicks for itself by default (it needs to,
        // to detect a double-click into inline-edit mode even when not
        // currently editable), which otherwise swallows every click/drag
        // before it ever reaches this row's own mouseDown/mouseDrag/mouseUp
        // -- exactly why rows were neither clickable nor draggable despite
        // this class's own handlers being entirely correct.
        nameLabel_.setInterceptsMouseClicks(false, false);
        nameLabel_.setText(descriptor_.displayName, juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        kindLabel_.setInterceptsMouseClicks(false, false);
        kindLabel_.setText(creation::assets::toDisplayName(descriptor_.kind), juce::dontSendNotification);
        kindLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        addAndMakeVisible(kindLabel_);

        sizeLabel_.setInterceptsMouseClicks(false, false);
        sizeLabel_.setText(FormatFileSize(descriptor_.fileSizeBytes), juce::dontSendNotification);
        sizeLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff6c7a8c));
        sizeLabel_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(sizeLabel_);

        modifiedLabel_.setInterceptsMouseClicks(false, false);
        modifiedLabel_.setText(descriptor_.modifiedAt.formatted("%Y-%m-%d %H:%M"), juce::dontSendNotification);
        modifiedLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff6c7a8c));
        modifiedLabel_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(modifiedLabel_);

        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        if (placeable_)
            setTooltip("Drag into the Scene Viewport to place it. Right-click for more actions.");
        else
            setTooltip("Right-click for more actions.");
    }

    void resized() override {
        auto bounds = getLocalBounds();
        modifiedLabel_.setBounds(bounds.removeFromRight(120));
        sizeLabel_.setBounds(bounds.removeFromRight(64));
        kindLabel_.setBounds(bounds.removeFromRight(140));
        nameLabel_.setBounds(bounds);
    }

    // Right-click shows the actions menu (Delete/Export/Reimport/Rename);
    // a plain click (that wasn't a drag) opens the asset -- e.g. a Pod row
    // opens straight into the Pod editor. A drag that moves far enough is
    // handled in mouseDrag instead, below.
    void mouseUp(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            owner_.ShowRowContextMenu(descriptor_);
        } else if (!draggedThisGesture_) {
            owner_.OpenAsset(descriptor_);
        }
        draggedThisGesture_ = false;
    }

    // Only placeable kinds (Render, Object Definition) start a real OS-
    // level drag; everything else's mouseDrag is a no-op, so e.g. a Pod
    // row can't be dragged onto the viewport where nothing would happen
    // with it. See ViewportComponent::isInterestedInDragSource for the
    // description format this must match.
    void mouseDrag(const juce::MouseEvent& event) override {
        if (!placeable_ || draggedThisGesture_) return;
        if (event.getDistanceFromDragStart() < 6) return;
        draggedThisGesture_ = true;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
            const juce::String description =
                "asset|" + creation::assets::toStorageToken(descriptor_.kind) + "|" + descriptor_.id + "|" +
                descriptor_.versionId + "|" + descriptor_.displayName;
            container->startDragging(description, this);
        }
    }

private:
    ContentBrowserPanel& owner_;
    creation::assets::AssetDescriptor descriptor_;
    bool placeable_;
    bool draggedThisGesture_ = false;
    juce::Label nameLabel_;
    juce::Label kindLabel_;
    juce::Label sizeLabel_;
    juce::Label modifiedLabel_;
};

ContentBrowserPanel::ContentBrowserPanel(ViewportComponent& viewport, ImportPanel& importPanel, frust::PodCatalog& podCatalog,
                                         scene::ObjectDefinitionCatalog& objectDefinitions)
    : viewport_(viewport), importPanel_(importPanel), podCatalog_(podCatalog), objectDefinitions_(objectDefinitions) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    // Editor UI/Workflow Overhaul plan, Phase 4: the one visible entry
    // point for creating anything (Decision 2) -- no more hidden right-
    // click-only affordance on a section header.
    createMenuButton_.onClick = [this] { ShowCreateMenu(); };
    createMenuButton_.setTooltip("Create a new Pod or Object Definition.");
    addAndMakeVisible(createMenuButton_);

    importButton_.onClick = [this] { importPanel_.BrowseAndImport(); };
    importButton_.setTooltip("Import an asset (or drag a file anywhere onto this panel).");
    addAndMakeVisible(importButton_);

    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hintLabel_);

    searchBox_.setTextToShowWhenEmpty("Filter by name...", juce::Colours::grey);
    searchBox_.onTextChange = [this] { Refresh(); };
    addAndMakeVisible(searchBox_);

    searchButton_.onClick = [this] { Refresh(); };
    searchButton_.setTooltip("Search");
    addAndMakeVisible(searchButton_);

    emptyLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(emptyLabel_);

    addAndMakeVisible(scrollView_);
    scrollView_.setViewedComponent(&rowsHost_, false);
    scrollView_.setScrollBarsShown(true, false);
}

ContentBrowserPanel::~ContentBrowserPanel() = default;

void ContentBrowserPanel::ShowCreateMenu() {
    if (projectSession_ == nullptr || !projectSession_->isValid()) return;

    juce::PopupMenu menu;
    menu.addItem(1, "New Behavior Pod");
    menu.addItem(2, "New Processing Pod");
    menu.addItem(3, "New Object Definition");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&createMenuButton_), [this](int result) {
        if (result == 1) CreateNewPod(frust::PodKind::Behavior);
        else if (result == 2) CreateNewPod(frust::PodKind::Processing);
        else if (result == 3) CreateNewObjectDefinition();
    });
}

void ContentBrowserPanel::ShowRowContextMenu(const creation::assets::AssetDescriptor& descriptor) {
    const bool renameable = descriptor.kind == creation::assets::AssetKind::game ||
                             descriptor.kind == creation::assets::AssetKind::scene;

    juce::PopupMenu menu;
    int nextId = 1;
    const int renameId = renameable ? nextId++ : -1;
    const int exportId = nextId++;
    const int reimportId = nextId++;
    const int deleteId = nextId++;
    if (renameable) menu.addItem(renameId, "Rename...");
    menu.addItem(exportId, "Export...");
    menu.addItem(reimportId, "Reimport...");
    menu.addSeparator();
    menu.addItem(deleteId, "Delete...");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, descriptor, renameId, exportId, reimportId, deleteId](int result) {
        if (result == renameId) RenameAsset(descriptor);
        else if (result == exportId) ExportAsset(descriptor);
        else if (result == reimportId) ReimportAsset(descriptor);
        else if (result == deleteId) DeleteAsset(descriptor);
    });
}

void ContentBrowserPanel::RenameAsset(const creation::assets::AssetDescriptor& descriptor) {
    if (descriptor.kind == creation::assets::AssetKind::game) {
        if (onGameRenameRequested) onGameRenameRequested(descriptor.id);
    } else if (descriptor.kind == creation::assets::AssetKind::scene) {
        if (onSceneRenameRequested) onSceneRenameRequested(descriptor.id);
    }
}

void ContentBrowserPanel::SetProjectContent(creation::assets::ProjectSession* session) {
    projectSession_ = session;
    Refresh();
}

void ContentBrowserPanel::Refresh() {
    const bool projectOpen = projectSession_ != nullptr && projectSession_->isValid();
    emptyLabel_.setVisible(!projectOpen);
    if (!projectOpen) {
        rows_.clear();
        resized();
        return;
    }

    // Keep only the latest (highest revision) descriptor per logical asset
    // id -- query({}) returns every version of every asset, and this panel
    // browses assets, not their reimport history.
    std::unordered_map<juce::String, creation::assets::AssetDescriptor> latestById;
    for (const auto& descriptor : projectSession_->getManifest().assetCatalog.query({})) {
        auto& slot = latestById[descriptor.id];
        if (slot.id.isEmpty() || descriptor.revision > slot.revision) {
            slot = descriptor;
        }
    }

    const auto filterText = searchBox_.getText().trim();
    std::vector<creation::assets::AssetDescriptor> descriptors;
    for (const auto& [id, descriptor] : latestById) {
        if (filterText.isNotEmpty() && !descriptor.displayName.containsIgnoreCase(filterText)) continue;
        descriptors.push_back(descriptor);
    }
    // Editor UI/Workflow Overhaul plan, Phase 4: one flat, alphabetically-
    // sorted list -- no AssetKind grouping (see the class comment above).
    std::sort(descriptors.begin(), descriptors.end(), [](const auto& a, const auto& b) {
        return a.displayName.compareIgnoreCase(b.displayName) < 0;
    });

    rows_.clear();
    for (const auto& descriptor : descriptors) {
        auto* row = rows_.add(new AssetRow(*this, descriptor));
        rowsHost_.addAndMakeVisible(row);
    }

    resized();
}

void ContentBrowserPanel::OpenAsset(const creation::assets::AssetDescriptor& descriptor) {
    if (onAssetOpened) onAssetOpened(descriptor);
}

void ContentBrowserPanel::CreateNewPod(frust::PodKind kind) {
    if (projectSession_ == nullptr || !projectSession_->isValid()) return;

    const auto name = GenerateDefaultPodName(podCatalog_, kind);
    podCatalog_.GetOrCreateGraph(name, kind);

    juce::String error;
    if (!podCatalog_.Save(*projectSession_, name, error)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Cannot Create Pod",
                                                "Could not save the new Pod: " + error);
        return;
    }

    Refresh();
    if (onPodCreated) onPodCreated(name);
}

void ContentBrowserPanel::CreateNewObjectDefinition() {
    if (projectSession_ == nullptr || !projectSession_->isValid()) return;

    const auto name = GenerateDefaultObjectDefinitionName(objectDefinitions_);
    scene::ObjectDefinition definition;
    definition.id = name;
    definition.displayName = name;

    juce::String upsertError;
    if (!objectDefinitions_.upsert(definition, upsertError)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Cannot Create Object Definition",
                                                "Could not create the new Object Definition: " + upsertError);
        return;
    }

    juce::String saveError;
    if (!objectDefinitions_.Save(*projectSession_, name, saveError)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Cannot Create Object Definition",
                                                "Could not save the new Object Definition: " + saveError);
        return;
    }

    Refresh();
    if (onObjectDefinitionCreated) onObjectDefinitionCreated(name);
}

void ContentBrowserPanel::DeleteAsset(const creation::assets::AssetDescriptor& latest) {
    if (projectSession_ == nullptr) return;

    const auto dependents = projectSession_->getManifest().assetCatalog.findDependents(latest.id);

    if (dependents.isEmpty()) {
        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon, "Delete Asset",
            "Delete \"" + latest.displayName + "\"? This cannot be undone.", "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create([this, latest](int result) {
                if (result == 1) PerformDelete(latest);
            }));
        return;
    }

    juce::String dependentList;
    for (const auto& dependent : dependents) dependentList += "\n  - " + dependent.displayName;

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon, "Asset In Use",
        "\"" + latest.displayName + "\" is still used by:" + dependentList +
            "\n\nDeleting it will leave those with a missing reference. Delete anyway?",
        "Delete Anyway", "Cancel", this,
        juce::ModalCallbackFunction::create([this, latest](int result) {
            if (result == 1) PerformDelete(latest);
        }));
}

void ContentBrowserPanel::PerformDelete(const creation::assets::AssetDescriptor& latest) {
    if (projectSession_ == nullptr) return;

    // Game/Scene need the games.xml cascade (reassigning entrySceneId,
    // rejecting a Game/project's last remaining Scene/Game, removing every
    // scene under a deleted game) -- not the generic per-version removal
    // below, which knows nothing about that structure.
    if (latest.kind == creation::assets::AssetKind::game) {
        if (onGameDeleteRequested) onGameDeleteRequested(latest.id);
        return;
    }
    if (latest.kind == creation::assets::AssetKind::scene) {
        if (onSceneDeleteRequested) onSceneDeleteRequested(latest.id);
        return;
    }

    // Every version, not just the latest -- "delete" per this project's
    // stated intent (no recycle bin) means gone, not "gone except for the
    // reimport history nobody can see or reach anymore."
    bool anyFailure = false;
    for (const auto& version : projectSession_->getManifest().assetCatalog.findAllVersions(latest.id)) {
        if (!projectSession_->removeEntry(version.logicalPath)) anyFailure = true;
        if (!projectSession_->removeAssetDescriptorByVersionId(version.versionId)) anyFailure = true;
    }

    juce::String errorMessage;
    if (!projectSession_->commit(errorMessage) || anyFailure) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Delete Failed",
            errorMessage.isNotEmpty() ? errorMessage : juce::String("The asset could not be fully removed."));
    }

    // Evict the runtime cache too -- otherwise the deleted asset stays
    // placeable/audible until the app restarts, even though it's gone from
    // the project. Keyed by displayName because every importer sets
    // displayName = sourceFile.getFileNameWithoutExtension(), the exact
    // same string it registers the runtime cache entry under.
    if (latest.kind == creation::assets::AssetKind::audio) {
        importPanel_.GetAudioCatalog().Remove(latest.displayName);
        importPanel_.RefreshAudioClips();
    } else {
        viewport_.Catalog().Remove(latest.displayName);
    }

    Refresh();
}

void ContentBrowserPanel::ReimportAsset(const creation::assets::AssetDescriptor& latest) {
    if (projectSession_ == nullptr) return;

    if (latest.derivationKind != creation::assets::AssetDerivationKind::root ||
        latest.externalSourcePath.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Cannot Reimport",
            "\"" + latest.displayName + "\" has no external source file on record to reimport from.");
        return;
    }

    const juce::File sourceFile(latest.externalSourcePath);
    if (sourceFile.existsAsFile()) {
        RunReimport(latest, sourceFile);
        return;
    }

    // The remembered source is gone (moved/deleted/on an unmounted drive)
    // -- offer to relocate it rather than dead-ending, per the pipeline
    // model's stated intent for this exact case.
    activeRelocateChooser_ = std::make_unique<juce::FileChooser>(
        "Locate \"" + latest.displayName + "\" -- its original file wasn't found at:\n" + latest.externalSourcePath,
        juce::File(latest.externalSourcePath).getParentDirectory());

    activeRelocateChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, latest](const juce::FileChooser& chooser) {
            const auto chosen = chooser.getResult();
            activeRelocateChooser_.reset();
            if (chosen != juce::File{}) RunReimport(latest, chosen);
        });
}

void ContentBrowserPanel::RunReimport(const creation::assets::AssetDescriptor& latest, const juce::File& sourceFile) {
    auto* importer = importPanel_.GetImporterRegistry().FindFor(sourceFile);
    if (importer == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Reimport Failed",
                                                "No importer is registered for \"" + sourceFile.getFileName() + "\".");
        return;
    }

    // A fresh, minimal context -- Reimport() never places a scene entity
    // (see AssetImporter::Reimport's own doc), so unlike ImportPanel's own
    // context_ this doesn't need engine::World at all.
    import::ImportContext context;
    context.catalog = &viewport_.Catalog();
    context.viewport = &viewport_;
    context.projectSession = projectSession_;
    context.audioCatalog = &importPanel_.GetAudioCatalog();
    context.audioFormatManager = &importPanel_.GetAudioFormatManager();
    context.objectDefinitions = &objectDefinitions_;

    const auto result = importer->Reimport(sourceFile, latest, context);
    if (!result.success) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Reimport Failed", result.message);
    }

    if (latest.kind == creation::assets::AssetKind::audio) importPanel_.RefreshAudioClips();
    Refresh();
}

void ContentBrowserPanel::ExportAsset(const creation::assets::AssetDescriptor& latest) {
    if (projectSession_ == nullptr) return;

    juce::MemoryBlock data;
    if (!projectSession_->readEntry(latest.logicalPath, data)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Failed",
                                                "Could not read \"" + latest.displayName + "\" from the project.");
        return;
    }

    const auto extension = latest.logicalPath.fromLastOccurrenceOf(".", true, false);
    activeExportChooser_ = std::make_unique<juce::FileChooser>(
        "Export \"" + latest.displayName + "\"",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(latest.displayName + extension));

    activeExportChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [data](const juce::FileChooser& chooser) {
            const auto chosen = chooser.getResult();
            if (chosen == juce::File{}) return;
            if (!chosen.replaceWithData(data.getData(), data.getSize())) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Failed",
                                                        "Could not write to \"" + chosen.getFullPathName() + "\".");
            }
        });
}

bool ContentBrowserPanel::isInterestedInFileDrag(const juce::StringArray& files) {
    return importPanel_.isInterestedInFileDrag(files);
}

void ContentBrowserPanel::filesDropped(const juce::StringArray& files, int x, int y) {
    // importPanel_ still owns the actual import pipeline (registry,
    // metadata popup, animation options) -- this panel is just the one
    // surface that triggers it now, in place of the deleted standalone
    // Import screen. x/y are this panel's own drop coordinates, meaningless
    // to importPanel_ (which isn't on screen), so they're not forwarded.
    importPanel_.filesDropped(files, 0, 0);
}

void ContentBrowserPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void ContentBrowserPanel::resized() {
    static constexpr int kRowHeight = 28;

    auto area = getLocalBounds().reduced(16);
    titleLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);

    // Editor UI/Workflow Overhaul plan, Phase 4 (Decision 2): Create and
    // Import share their own top row -- neither is visually paired with
    // the search box below, which previously made Import look like part
    // of the filter control (a confirmed complaint).
    auto actionRow = area.removeFromTop(28);
    createMenuButton_.setBounds(actionRow.removeFromLeft(90));
    actionRow.removeFromLeft(8);
    importButton_.setBounds(actionRow.removeFromLeft(90));
    area.removeFromTop(8);

    hintLabel_.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);

    // Search sits on its own row directly above the list -- narrower than
    // full width, with an explicit search button after it.
    auto searchRow = area.removeFromTop(28);
    searchButton_.setBounds(searchRow.removeFromRight(28));
    searchRow.removeFromRight(6);
    searchBox_.setBounds(searchRow.removeFromLeft(220));
    area.removeFromTop(8);

    if (emptyLabel_.isVisible()) {
        emptyLabel_.setBounds(area.removeFromTop(40));
        scrollView_.setBounds(area);
        return;
    }

    scrollView_.setBounds(area);

    // scrollView_'s own vertical scrollbar (if shown) eats into its width;
    // getMaximumVisibleWidth() already accounts for that, so rowsHost_
    // never has to fight the scrollbar for the same pixels.
    const int hostWidth = scrollView_.getMaximumVisibleWidth();
    rowsHost_.setSize(hostWidth, rows_.size() * kRowHeight);

    int y = 0;
    for (auto* row : rows_) {
        row->setBounds(0, y, hostWidth, kRowHeight - 2);
        y += kRowHeight;
    }
}

} // namespace ce::views
