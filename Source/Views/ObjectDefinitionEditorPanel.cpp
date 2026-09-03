#include "ObjectDefinitionEditorPanel.h"

#include <algorithm>

namespace ce::views
{

// One attached Pod: its id, plus Remove. Mirrors PodEditorPanel's old
// InterfaceInputRow / PodInfoPanel's InterfaceInputRow shape.
class ObjectDefinitionEditorPanel::PodRow final : public juce::Component {
public:
    PodRow(ObjectDefinitionEditorPanel& owner, int index, juce::String podId) : owner_(owner), index_(index) {
        label_.setText(podId, juce::dontSendNotification);
        label_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(label_);

        removeButton_.onClick = [this] { owner_.RemovePodAt(index_); };
        addAndMakeVisible(removeButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        removeButton_.setBounds(bounds.removeFromRight(24).reduced(1));
        label_.setBounds(bounds);
    }

private:
    ObjectDefinitionEditorPanel& owner_;
    int index_;
    juce::Label label_;
    juce::TextButton removeButton_{ "x" };
};

ObjectDefinitionEditorPanel::ObjectDefinitionEditorPanel(scene::ObjectDefinitionCatalog& catalog, frust::PodCatalog& podCatalog,
                                                         creation::assets::ProjectSession& projectSession)
    : catalog_(catalog), podCatalog_(podCatalog), projectSession_(projectSession)
{
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    nameLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(nameLabel_);

    meshSectionLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    meshSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(meshSectionLabel_);

    meshLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(meshLabel_);

    pickMeshButton_.onClick = [this] { PickMesh(); };
    addAndMakeVisible(pickMeshButton_);

    podsSectionLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    podsSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(podsSectionLabel_);

    addPodButton_.onClick = [this] { AddPod(); };
    addAndMakeVisible(addPodButton_);

    saveButton_.onClick = [this] { SaveContent(); };
    saveButton_.setTooltip("Save this Object Definition to the project.");
    addAndMakeVisible(saveButton_);

    status_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(status_);
}

ObjectDefinitionEditorPanel::~ObjectDefinitionEditorPanel() = default;

void ObjectDefinitionEditorPanel::OpenDefinition(const juce::String& id) {
    openId_ = id;
    nameLabel_.setText(id, juce::dontSendNotification);
    status_.setText({}, juce::dontSendNotification);

    const auto* definition = catalog_.find(id);
    meshAssetId_ = definition ? definition->meshAssetId : juce::String();
    meshAssetVersionId_ = definition ? definition->meshAssetVersionId : juce::String();
    attachedPods_ = definition ? definition->behaviorPods : std::vector<juce::String>();

    meshDisplayName_.clear();
    if (meshAssetId_.isNotEmpty() && projectSession_.isValid()) {
        for (const auto& asset : projectSession_.getManifest().assetCatalog.query({ creation::assets::AssetKind::render })) {
            if (asset.id == meshAssetId_) { meshDisplayName_ = asset.displayName; break; }
        }
    }
    meshLabel_.setText(meshDisplayName_.isNotEmpty() ? meshDisplayName_
                                                      : (meshAssetId_.isNotEmpty() ? "(unresolved: " + meshAssetId_ + ")" : "(none)"),
                        juce::dontSendNotification);

    RefreshPodRows();
    resized();
}

void ObjectDefinitionEditorPanel::PickMesh() {
    if (!projectSession_.isValid()) return;

    juce::PopupMenu menu;
    std::vector<creation::assets::AssetDescriptor> meshes;
    for (const auto& asset : projectSession_.getManifest().assetCatalog.query({ creation::assets::AssetKind::render })) {
        meshes.push_back(asset);
    }
    std::sort(meshes.begin(), meshes.end(), [](const auto& a, const auto& b) {
        return a.displayName.compareIgnoreCase(b.displayName) < 0;
    });
    if (meshes.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Meshes",
                                                "Import a mesh (Assets & Import) before assigning one here.");
        return;
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        menu.addItem(static_cast<int>(i) + 1, meshes[i].displayName);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&pickMeshButton_), [this, meshes](int result) {
        if (result <= 0 || result > static_cast<int>(meshes.size())) return;
        const auto& chosen = meshes[static_cast<std::size_t>(result - 1)];
        meshAssetId_ = chosen.id;
        meshAssetVersionId_ = chosen.versionId;
        meshDisplayName_ = chosen.displayName;
        meshLabel_.setText(meshDisplayName_, juce::dontSendNotification);
    });
}

void ObjectDefinitionEditorPanel::AddPod() {
    const auto podNames = podCatalog_.Names();
    if (podNames.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Pods",
                                                "Create a Pod (Content Browser's Pods section) before attaching one here.");
        return;
    }
    juce::PopupMenu menu;
    for (std::size_t i = 0; i < podNames.size(); ++i) {
        const bool alreadyAttached =
            std::find(attachedPods_.begin(), attachedPods_.end(), podNames[i]) != attachedPods_.end();
        menu.addItem(static_cast<int>(i) + 1, podNames[i], !alreadyAttached);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addPodButton_), [this, podNames](int result) {
        if (result <= 0 || result > static_cast<int>(podNames.size())) return;
        attachedPods_.push_back(podNames[static_cast<std::size_t>(result - 1)]);
        RefreshPodRows();
    });
}

void ObjectDefinitionEditorPanel::RemovePodAt(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= attachedPods_.size()) return;
    attachedPods_.erase(attachedPods_.begin() + index);
    RefreshPodRows();
}

void ObjectDefinitionEditorPanel::RefreshPodRows() {
    podRows_.clear();
    for (std::size_t i = 0; i < attachedPods_.size(); ++i) {
        auto* row = podRows_.add(new PodRow(*this, static_cast<int>(i), attachedPods_[i]));
        addAndMakeVisible(row);
    }
    resized();
}

void ObjectDefinitionEditorPanel::SaveContent() {
    if (openId_.isEmpty()) return;

    scene::ObjectDefinition definition;
    if (const auto* existing = catalog_.find(openId_)) definition = *existing;
    definition.id = openId_;
    if (definition.displayName.isEmpty()) definition.displayName = openId_;
    definition.meshAssetId = meshAssetId_;
    definition.meshAssetVersionId = meshAssetVersionId_;
    definition.behaviorPods = attachedPods_;

    juce::String upsertError;
    if (!catalog_.upsert(definition, upsertError)) {
        status_.setText("Save failed: " + upsertError, juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }

    juce::String saveError;
    if (!catalog_.Save(projectSession_, openId_, saveError)) {
        status_.setText("Saved locally, but could not persist to the project: " + saveError, juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, juce::Colour(0xffffb454));
        return;
    }

    status_.setText("Saved \"" + openId_ + "\"", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
}

void ObjectDefinitionEditorPanel::resized() {
    auto area = getLocalBounds().reduced(16, 12);

    titleLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    nameLabel_.setBounds(area.removeFromTop(22));
    area.removeFromTop(12);

    meshSectionLabel_.setBounds(area.removeFromTop(18));
    auto meshRow = area.removeFromTop(26);
    pickMeshButton_.setBounds(meshRow.removeFromRight(110).reduced(1));
    meshRow.removeFromRight(8);
    meshLabel_.setBounds(meshRow);
    area.removeFromTop(12);

    podsSectionLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(2);
    for (auto* row : podRows_) {
        row->setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
    }
    addPodButton_.setBounds(area.removeFromTop(24));
    area.removeFromTop(12);

    auto footer = area.removeFromTop(26);
    saveButton_.setBounds(footer.removeFromLeft(80));
    footer.removeFromLeft(8);
    status_.setBounds(footer);
}

void ObjectDefinitionEditorPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff10141a));
}

} // namespace ce::views
