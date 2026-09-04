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

// One row: the durable project asset's display name/kind/category, plus
// Delete. Deliberately shows only the LATEST version of each logical asset
// -- older reimport history exists (AssetCatalog::findAllVersions) but
// isn't browsable UI yet, same "not every real field needs a row today"
// scoping ImportPanel's own AI6 section applied to animation options.
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

        // Kind is already the enclosing Section's header -- repeating it
        // per row would be the same redundant-column mistake the node
        // palette had. Category, size, and modified time show here instead
        // -- real metadata, not just a name (the whole point of this being
        // a file organizer and not a bare list).
        categoryLabel_.setInterceptsMouseClicks(false, false);
        categoryLabel_.setText(descriptor_.category, juce::dontSendNotification);
        categoryLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        addAndMakeVisible(categoryLabel_);

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

        deleteButton_.onClick = [this] { owner_.DeleteAsset(descriptor_); };
        addAndMakeVisible(deleteButton_);

        exportButton_.onClick = [this] { owner_.ExportAsset(descriptor_); };
        exportButton_.setTooltip("Save this asset's stored content out to a file.");
        addAndMakeVisible(exportButton_);

        reimportButton_.onClick = [this] { owner_.ReimportAsset(descriptor_); };
        reimportButton_.setTooltip("Re-read this asset's source file (or locate it, if it's moved) and save the "
                                    "result as a new version.");
        addAndMakeVisible(reimportButton_);

        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        if (placeable_)
            setTooltip("Drag into the Scene Viewport to place it.");
    }

    void resized() override {
        auto bounds = getLocalBounds();
        deleteButton_.setBounds(bounds.removeFromRight(60).reduced(2));
        exportButton_.setBounds(bounds.removeFromRight(64).reduced(2));
        reimportButton_.setBounds(bounds.removeFromRight(76).reduced(2));
        modifiedLabel_.setBounds(bounds.removeFromRight(120));
        sizeLabel_.setBounds(bounds.removeFromRight(64));
        categoryLabel_.setBounds(bounds.removeFromRight(140));
        nameLabel_.setBounds(bounds);
    }

    // Click anywhere on the row outside of Delete/Export/Reimport (those
    // are child buttons and consume their own clicks first) opens the
    // asset -- e.g. a Pod row opens straight into the Pod editor. A drag
    // that moves far enough is handled in mouseDrag instead, below.
    void mouseUp(const juce::MouseEvent&) override {
        if (!draggedThisGesture_) owner_.OpenAsset(descriptor_);
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
    juce::Label categoryLabel_;
    juce::Label sizeLabel_;
    juce::Label modifiedLabel_;
    juce::TextButton deleteButton_{ "Delete" };
    juce::TextButton exportButton_{ "Export" };
    juce::TextButton reimportButton_{ "Reimport" };
};

// One collapsible section per AssetKind -- a header (kind name, count,
// expand/collapse chevron) plus that kind's own rows. Right-clicking
// anywhere in the Pods section (header, or the empty-hint area when it
// has zero rows) is the entire "New Pod" creation flow -- there is no
// name box and no button elsewhere. Kept as one persistent object per
// kind across Refresh() calls (rows rebuilt in place via SetRows(), the
// Section itself never recreated) so expand/collapse state survives a
// Pod being created, saved, or deleted.
class ContentBrowserPanel::Section final : public juce::Component {
public:
    static constexpr int kHeaderHeight = 22;
    static constexpr int kRowHeight = 28;
    static constexpr int kHintHeight = 20;

    Section(ContentBrowserPanel& owner, creation::assets::AssetKind kind) : owner_(owner), kind_(kind) {}

    creation::assets::AssetKind Kind() const { return kind_; }
    bool IsEmpty() const { return rows_.isEmpty(); }

    void SetRows(std::vector<creation::assets::AssetDescriptor> descriptors) {
        rows_.clear();
        for (auto& descriptor : descriptors) {
            auto* row = rows_.add(new AssetRow(owner_, descriptor));
            addAndMakeVisible(row);
        }
        resized();
        repaint();
    }

    int GetPreferredHeight() const {
        if (!expanded_) return kHeaderHeight;
        if (rows_.isEmpty()) return kHeaderHeight + (HasCreationMenu() ? kHintHeight : 0);
        return kHeaderHeight + rows_.size() * kRowHeight;
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(kHeaderHeight);
        if (!expanded_) return;
        for (auto* row : rows_) {
            row->setBounds(bounds.removeFromTop(kRowHeight - 2));
            bounds.removeFromTop(2);
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        auto header = bounds.removeFromTop(kHeaderHeight);
        g.setColour(juce::Colour(0xff20262f));
        g.fillRect(header);
        g.setColour(juce::Colour(0xff9aa8ba));
        g.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        const juce::String chevron = expanded_ ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc "))
                                                : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "));
        g.drawText(chevron + creation::assets::toDisplayName(kind_) + "  (" + juce::String(rows_.size()) + ")",
                   header.reduced(8, 0), juce::Justification::centredLeft, true);

        if (expanded_ && rows_.isEmpty() && HasCreationMenu()) {
            g.setColour(juce::Colour(0xff5c6b7d));
            g.setFont(juce::Font(juce::FontOptions(11.0f)).italicised());
            g.drawText("Right-click to create a new " + creation::assets::toDisplayName(kind_), bounds.reduced(8, 0),
                       juce::Justification::centredLeft, true);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            ShowContextMenu();
            return;
        }
        if (event.getPosition().y < kHeaderHeight) {
            expanded_ = !expanded_;
            owner_.resized();
            repaint();
        }
    }

private:
    // Which kinds currently have a right-click "New" affordance -- other
    // kinds are populated via Import instead, no creation menu needed for
    // them yet (see the class comment above).
    bool HasCreationMenu() const {
        return kind_ == creation::assets::AssetKind::pod || kind_ == creation::assets::AssetKind::objectDefinition;
    }

    void ShowContextMenu() {
        if (kind_ == creation::assets::AssetKind::pod) {
            juce::PopupMenu menu;
            menu.addItem(1, "New Behavior Pod");
            menu.addItem(2, "New Processing Pod");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result) {
                if (result == 1) owner_.CreateNewPod(frust::PodKind::Behavior);
                else if (result == 2) owner_.CreateNewPod(frust::PodKind::Processing);
            });
        } else if (kind_ == creation::assets::AssetKind::objectDefinition) {
            juce::PopupMenu menu;
            menu.addItem(1, "New Object Definition");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result) {
                if (result == 1) owner_.CreateNewObjectDefinition();
            });
        }
    }

    ContentBrowserPanel& owner_;
    creation::assets::AssetKind kind_;
    bool expanded_ = true;
    juce::OwnedArray<AssetRow> rows_;
};

ContentBrowserPanel::ContentBrowserPanel(ViewportComponent& viewport, ImportPanel& importPanel, frust::PodCatalog& podCatalog,
                                         scene::ObjectDefinitionCatalog& objectDefinitions)
    : viewport_(viewport), importPanel_(importPanel), podCatalog_(podCatalog), objectDefinitions_(objectDefinitions) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hintLabel_);

    searchBox_.setTextToShowWhenEmpty("Filter by name...", juce::Colours::grey);
    searchBox_.onTextChange = [this] { Refresh(); };
    addAndMakeVisible(searchBox_);

    importButton_.onClick = [this] { importPanel_.BrowseAndImport(); };
    importButton_.setTooltip("Import an asset (or drag a file anywhere onto this panel).");
    addAndMakeVisible(importButton_);

    emptyLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(emptyLabel_);

    addAndMakeVisible(scrollView_);
    scrollView_.setViewedComponent(&sectionsHost_, false);
    scrollView_.setScrollBarsShown(true, false);

    // Sections with a "New" affordance always exist -- created once here,
    // never removed -- so there's always somewhere to right-click even
    // before a project is open or before any asset of that kind exists.
    FindOrCreateSection(creation::assets::AssetKind::pod);
    FindOrCreateSection(creation::assets::AssetKind::objectDefinition);
    // Game/Scene get no right-click "New" here (see HasCreationMenu) --
    // creation stays where it already lives, the File menu / ExplorerPanel's
    // New Game/New Scene buttons -- but still always-present sections, same
    // reasoning as Pod/Object Definition above.
    FindOrCreateSection(creation::assets::AssetKind::game);
    FindOrCreateSection(creation::assets::AssetKind::scene);
}

ContentBrowserPanel::~ContentBrowserPanel() = default;

void ContentBrowserPanel::SetProjectContent(creation::assets::ProjectSession* session) {
    projectSession_ = session;
    Refresh();
}

ContentBrowserPanel::Section* ContentBrowserPanel::FindOrCreateSection(creation::assets::AssetKind kind) {
    for (auto* section : sections_)
        if (section->Kind() == kind) return section;
    auto* section = sections_.add(new Section(*this, kind));
    sectionsHost_.addAndMakeVisible(section);
    return section;
}

void ContentBrowserPanel::Refresh() {
    const bool projectOpen = projectSession_ != nullptr && projectSession_->isValid();
    emptyLabel_.setVisible(!projectOpen);
    if (!projectOpen) {
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
    std::unordered_map<creation::assets::AssetKind, std::vector<creation::assets::AssetDescriptor>> byKind;
    byKind[creation::assets::AssetKind::pod]; // always present, even filtered/empty.
    byKind[creation::assets::AssetKind::objectDefinition]; // same.
    byKind[creation::assets::AssetKind::game]; // same.
    byKind[creation::assets::AssetKind::scene]; // same.
    for (const auto& [id, descriptor] : latestById) {
        if (filterText.isNotEmpty() && !descriptor.displayName.containsIgnoreCase(filterText)) continue;
        byKind[descriptor.kind].push_back(descriptor);
    }

    for (auto& [kind, descriptors] : byKind) {
        std::sort(descriptors.begin(), descriptors.end(), [](const auto& a, const auto& b) {
            return a.displayName.compareIgnoreCase(b.displayName) < 0;
        });
        FindOrCreateSection(kind)->SetRows(std::move(descriptors));
    }

    // Drop sections for kinds that no longer have any (matching) assets --
    // except the ones that always stay (see the constructor).
    for (int i = sections_.size() - 1; i >= 0; --i) {
        auto* section = sections_.getUnchecked(i);
        const bool alwaysStays = section->Kind() == creation::assets::AssetKind::pod ||
                                  section->Kind() == creation::assets::AssetKind::objectDefinition ||
                                  section->Kind() == creation::assets::AssetKind::game ||
                                  section->Kind() == creation::assets::AssetKind::scene;
        if (!alwaysStays && section->IsEmpty()) sections_.remove(i);
    }

    struct SectionComparator {
        static int compareElements(Section* a, Section* b) {
            const bool aPods = a->Kind() == creation::assets::AssetKind::pod;
            const bool bPods = b->Kind() == creation::assets::AssetKind::pod;
            if (aPods != bPods) return aPods ? -1 : 1;
            return creation::assets::toDisplayName(a->Kind()).compareIgnoreCase(creation::assets::toDisplayName(b->Kind()));
        }
    } comparator;
    sections_.sort(comparator);

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
    auto area = getLocalBounds().reduced(16);
    titleLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    hintLabel_.setBounds(area.removeFromTop(20));
    area.removeFromTop(8);
    auto searchRow = area.removeFromTop(28);
    importButton_.setBounds(searchRow.removeFromRight(90));
    searchRow.removeFromRight(8);
    searchBox_.setBounds(searchRow);
    area.removeFromTop(8);

    if (emptyLabel_.isVisible()) {
        emptyLabel_.setBounds(area.removeFromTop(40));
        scrollView_.setBounds(area);
        return;
    }

    scrollView_.setBounds(area);

    // scrollView_'s own vertical scrollbar (if shown) eats into its width;
    // getMaximumVisibleWidth() already accounts for that, so sectionsHost_
    // never has to fight the scrollbar for the same pixels.
    const int hostWidth = scrollView_.getMaximumVisibleWidth();
    int totalHeight = 0;
    for (auto* section : sections_) totalHeight += section->GetPreferredHeight() + 4;
    sectionsHost_.setSize(hostWidth, totalHeight);

    int y = 0;
    for (auto* section : sections_) {
        const auto height = section->GetPreferredHeight();
        section->setBounds(0, y, hostWidth, height);
        y += height + 4;
    }
}

} // namespace ce::views
