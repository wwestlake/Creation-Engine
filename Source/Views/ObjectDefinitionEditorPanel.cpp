#include "ObjectDefinitionEditorPanel.h"

#include <algorithm>

namespace ce::views
{

// One component-list entry: its resolved display text (already kind-
// prefixed, e.g. "Mesh: Barrel", "Pod: Rotator", "Child: TableLamp"), plus
// Remove. One row shape serves all three kinds -- per docs/OBJECT_MODEL.md
// they're the same kind of list entry, distinguished only by what they
// reference, not by having different UI.
class ObjectDefinitionEditorPanel::ComponentRow final : public juce::Component {
public:
    ComponentRow(ObjectDefinitionEditorPanel& owner, int index, juce::String displayText) : owner_(owner), index_(index) {
        label_.setText(displayText, juce::dontSendNotification);
        label_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(label_);

        removeButton_.onClick = [this] { owner_.RemoveComponentAt(index_); };
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

    editorOnlyToggle_.setTooltip("Instances of this definition are hidden (not despawned) while Play is active -- e.g. the VR edit-mode cart.");
    addAndMakeVisible(editorOnlyToggle_);

    componentsSectionLabel_.setFont(juce::Font(juce::FontOptions(13.0f)).boldened());
    componentsSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(componentsSectionLabel_);

    addComponentButton_.onClick = [this] { AddComponent(); };
    addAndMakeVisible(addComponentButton_);

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
    components_ = definition ? definition->components : std::vector<scene::ObjectComponentEntry>();
    editorOnlyToggle_.setToggleState(definition && definition->editorOnly, juce::dontSendNotification);

    RefreshComponentRows();
    resized();
}

void ObjectDefinitionEditorPanel::AddComponent() {
    juce::PopupMenu menu;
    menu.addItem(1, "Mesh Reference...");
    menu.addItem(2, "Pod...");
    menu.addItem(3, "Child Object...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addComponentButton_), [this](int result) {
        if (result == 1) PickMesh();
        else if (result == 2) AddPod();
        else if (result == 3) PickChildDefinition();
    });
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addComponentButton_), [this, meshes](int result) {
        if (result <= 0 || result > static_cast<int>(meshes.size())) return;
        const auto& chosen = meshes[static_cast<std::size_t>(result - 1)];
        scene::ObjectComponentEntry entry;
        entry.kind = scene::ObjectComponentKind::Mesh;
        entry.meshAssetId = chosen.id;
        entry.meshAssetVersionId = chosen.versionId;
        components_.push_back(std::move(entry));
        RefreshComponentRows();
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
        const bool alreadyAttached = std::any_of(components_.begin(), components_.end(), [&](const auto& component) {
            return component.kind == scene::ObjectComponentKind::Pod && component.podId == podNames[i];
        });
        menu.addItem(static_cast<int>(i) + 1, podNames[i], !alreadyAttached);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addComponentButton_), [this, podNames](int result) {
        if (result <= 0 || result > static_cast<int>(podNames.size())) return;
        scene::ObjectComponentEntry entry;
        entry.kind = scene::ObjectComponentKind::Pod;
        entry.podId = podNames[static_cast<std::size_t>(result - 1)];
        components_.push_back(std::move(entry));
        RefreshComponentRows();
    });
}

void ObjectDefinitionEditorPanel::PickChildDefinition() {
    auto ids = catalog_.ids();
    ids.erase(std::remove(ids.begin(), ids.end(), openId_), ids.end());
    if (ids.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "No Other Object Definitions",
                                                "Create another Object Definition (Content Browser's Object Definitions "
                                                "section) before nesting one here.");
        return;
    }
    std::sort(ids.begin(), ids.end(), [](const auto& a, const auto& b) { return a.compareIgnoreCase(b) < 0; });

    juce::PopupMenu menu;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        menu.addItem(static_cast<int>(i) + 1, ids[i]);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addComponentButton_), [this, ids](int result) {
        if (result <= 0 || result > static_cast<int>(ids.size())) return;
        scene::ObjectComponentEntry entry;
        entry.kind = scene::ObjectComponentKind::Child;
        entry.childDefinitionId = ids[static_cast<std::size_t>(result - 1)];
        // Identity transform -- editing a child's local placement isn't
        // exposed in this pass; see the plan's Phase 3 note.
        components_.push_back(std::move(entry));
        RefreshComponentRows();
    });
}

void ObjectDefinitionEditorPanel::RemoveComponentAt(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= components_.size()) return;
    components_.erase(components_.begin() + index);
    RefreshComponentRows();
}

void ObjectDefinitionEditorPanel::RefreshComponentRows() {
    componentRows_.clear();
    for (std::size_t i = 0; i < components_.size(); ++i) {
        const auto& component = components_[i];
        juce::String displayText;
        switch (component.kind) {
            case scene::ObjectComponentKind::Mesh: {
                juce::String meshDisplayName;
                if (projectSession_.isValid()) {
                    for (const auto& asset : projectSession_.getManifest().assetCatalog.query({ creation::assets::AssetKind::render })) {
                        if (asset.id == component.meshAssetId) { meshDisplayName = asset.displayName; break; }
                    }
                }
                displayText = "Mesh: " + (meshDisplayName.isNotEmpty() ? meshDisplayName : component.meshAssetId);
                break;
            }
            case scene::ObjectComponentKind::Pod:
                displayText = "Pod: " + component.podId;
                break;
            case scene::ObjectComponentKind::Child:
                displayText = "Child: " + component.childDefinitionId;
                break;
        }
        auto* row = componentRows_.add(new ComponentRow(*this, static_cast<int>(i), displayText));
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
    definition.components = components_;
    definition.editorOnly = editorOnlyToggle_.getToggleState();

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

    editorOnlyToggle_.setBounds(area.removeFromTop(22));
    area.removeFromTop(12);

    componentsSectionLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(2);
    for (auto* row : componentRows_) {
        row->setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
    }
    addComponentButton_.setBounds(area.removeFromTop(24));
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
