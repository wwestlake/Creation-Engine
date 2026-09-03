#include "Views/ContentBrowserPanel.h"

#include <algorithm>
#include <unordered_map>

#include <creation/assets/ProjectAssetService.h>

#include "Import/AssetImporter.h"
#include "Import/ImporterRegistry.h"
#include "Render/ViewportComponent.h"
#include "Scene/AssetCatalog.h"

namespace ce::views {

// One row: the durable project asset's display name/kind/category, plus
// Delete. Deliberately shows only the LATEST version of each logical asset
// -- older reimport history exists (AssetCatalog::findAllVersions) but
// isn't browsable UI yet, same "not every real field needs a row today"
// scoping ImportPanel's own AI6 section applied to animation options.
class ContentBrowserPanel::AssetRow final : public juce::Component {
public:
    AssetRow(ContentBrowserPanel& owner, creation::assets::AssetDescriptor descriptor)
        : owner_(owner), descriptor_(std::move(descriptor)) {
        nameLabel_.setText(descriptor_.displayName, juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        kindLabel_.setText(creation::assets::toDisplayName(descriptor_.kind) +
                                (descriptor_.category.isNotEmpty() ? " / " + descriptor_.category : juce::String()),
                            juce::dontSendNotification);
        kindLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        addAndMakeVisible(kindLabel_);

        deleteButton_.onClick = [this] { owner_.DeleteAsset(descriptor_); };
        addAndMakeVisible(deleteButton_);

        reimportButton_.onClick = [this] { owner_.ReimportAsset(descriptor_); };
        reimportButton_.setTooltip("Re-read this asset's source file (or locate it, if it's moved) and save the "
                                    "result as a new version.");
        addAndMakeVisible(reimportButton_);

        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        deleteButton_.setBounds(bounds.removeFromRight(70).reduced(2));
        reimportButton_.setBounds(bounds.removeFromRight(84).reduced(2));
        kindLabel_.setBounds(bounds.removeFromRight(160));
        nameLabel_.setBounds(bounds);
    }

    // Click anywhere on the row outside of Delete/Reimport (those are
    // child buttons and consume their own clicks first) opens the asset
    // -- e.g. a Pod row opens straight into the Pod editor.
    void mouseUp(const juce::MouseEvent&) override { owner_.OpenAsset(descriptor_); }

private:
    ContentBrowserPanel& owner_;
    creation::assets::AssetDescriptor descriptor_;
    juce::Label nameLabel_;
    juce::Label kindLabel_;
    juce::TextButton deleteButton_{ "Delete" };
    juce::TextButton reimportButton_{ "Reimport" };
};

ContentBrowserPanel::ContentBrowserPanel(ViewportComponent& viewport, ImportPanel& importPanel)
    : viewport_(viewport), importPanel_(importPanel) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hintLabel_);

    searchBox_.setTextToShowWhenEmpty("Filter by name...", juce::Colours::grey);
    searchBox_.onTextChange = [this] { Refresh(); };
    addAndMakeVisible(searchBox_);

    emptyLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    emptyLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(emptyLabel_);
}

ContentBrowserPanel::~ContentBrowserPanel() = default;

void ContentBrowserPanel::SetProjectContent(creation::assets::ProjectSession* session) {
    projectSession_ = session;
    Refresh();
}

void ContentBrowserPanel::Refresh() {
    rows_.clear();

    if (projectSession_ == nullptr || !projectSession_->isValid()) {
        emptyLabel_.setText("Open a project to browse its assets.", juce::dontSendNotification);
        emptyLabel_.setVisible(true);
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
    std::vector<creation::assets::AssetDescriptor> visible;
    for (const auto& [id, descriptor] : latestById) {
        if (filterText.isNotEmpty() && !descriptor.displayName.containsIgnoreCase(filterText)) continue;
        visible.push_back(descriptor);
    }
    std::sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) {
        return a.displayName.compareIgnoreCase(b.displayName) < 0;
    });

    for (const auto& descriptor : visible) {
        auto* row = rows_.add(new AssetRow(*this, descriptor));
        addAndMakeVisible(row);
    }

    emptyLabel_.setText(filterText.isNotEmpty() ? "No assets match \"" + filterText + "\"."
                                                 : "No assets imported into this project yet -- see Assets & Import.",
                         juce::dontSendNotification);
    emptyLabel_.setVisible(rows_.isEmpty());
    resized();
}

void ContentBrowserPanel::OpenAsset(const creation::assets::AssetDescriptor& descriptor) {
    if (onAssetOpened) onAssetOpened(descriptor);
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

    const auto result = importer->Reimport(sourceFile, latest, context);
    if (!result.success) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Reimport Failed", result.message);
    }

    if (latest.kind == creation::assets::AssetKind::audio) importPanel_.RefreshAudioClips();
    Refresh();
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
    searchBox_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    if (rows_.isEmpty()) {
        emptyLabel_.setBounds(area.removeFromTop(40));
        return;
    }

    for (auto* row : rows_) {
        row->setBounds(area.removeFromTop(26));
        area.removeFromTop(2);
    }
}

} // namespace ce::views
