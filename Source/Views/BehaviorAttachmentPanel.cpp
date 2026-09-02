#include "Views/BehaviorAttachmentPanel.h"

#include <algorithm>
#include <mutex>

#include "Scene/Components.h"

namespace ce {

class BehaviorAttachmentPanel::AttachmentRow final : public juce::Component {
public:
    AttachmentRow(BehaviorAttachmentPanel& owner, juce::String podId) : owner_(owner), podId_(std::move(podId)) {
        nameLabel_.setText(podId_, juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel_);

        removeButton_.onClick = [this] { owner_.RemoveBehavior(podId_); };
        addAndMakeVisible(removeButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        removeButton_.setBounds(bounds.removeFromRight(70).reduced(2));
        nameLabel_.setBounds(bounds);
    }

private:
    BehaviorAttachmentPanel& owner_;
    juce::String podId_;
    juce::Label nameLabel_;
    juce::TextButton removeButton_ { "Remove" };
};

BehaviorAttachmentPanel::~BehaviorAttachmentPanel() = default;

BehaviorAttachmentPanel::BehaviorAttachmentPanel(engine::World& world, frust::EngineFrustHost& frustHost,
                                                 frust::PodCatalog& catalog)
    : world_(world), frustHost_(frustHost), catalog_(catalog) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noSelectionLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noSelectionLabel_);

    addAndMakeVisible(availableBehaviors_);
    attachButton_.onClick = [this] { AttachSelectedBehavior(); };
    addAndMakeVisible(attachButton_);
}

void BehaviorAttachmentPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
    Refresh();
}

void BehaviorAttachmentPanel::Refresh() {
    const bool hasSelection = selectedEntity_ != entt::null;
    noSelectionLabel_.setVisible(!hasSelection);
    availableBehaviors_.setVisible(hasSelection);
    attachButton_.setVisible(hasSelection);

    availableBehaviors_.clear(juce::dontSendNotification);
    auto names = catalog_.Names(frust::PodKind::Behavior);
    std::sort(names.begin(), names.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareIgnoreCase(b) < 0;
    });
    int itemId = 1;
    for (const auto& name : names) availableBehaviors_.addItem(name, itemId++);
    if (availableBehaviors_.getNumItems() > 0) availableBehaviors_.setSelectedItemIndex(0, juce::dontSendNotification);

    rows_.clear();
    if (hasSelection) {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(selectedEntity_)) {
            if (const auto* attachments = registry.try_get<scene::BehaviorAttachments>(selectedEntity_)) {
                for (const auto& podId : attachments->podIds) {
                    auto* row = rows_.add(new AttachmentRow(*this, podId));
                    addAndMakeVisible(row);
                }
            }
        }
    }
    resized();
}

void BehaviorAttachmentPanel::AttachSelectedBehavior() {
    if (selectedEntity_ == entt::null) return;
    const auto name = availableBehaviors_.getText();
    if (name.isEmpty()) return;

    if (!frustHost_.isObjectBehaviorLoaded(name.toStdString())) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Cannot Attach",
            "\"" + name + "\" has no loaded pod yet -- open it in FRust Logic and Compile first.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) return;
        auto& attachments = registry.get_or_emplace<scene::BehaviorAttachments>(selectedEntity_);
        if (std::find(attachments.podIds.begin(), attachments.podIds.end(), name) == attachments.podIds.end())
            attachments.podIds.push_back(name);
    }
    Refresh();
}

void BehaviorAttachmentPanel::RemoveBehavior(const juce::String& podId) {
    if (selectedEntity_ == entt::null) return;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) return;
        if (auto* attachments = registry.try_get<scene::BehaviorAttachments>(selectedEntity_)) {
            attachments->podIds.erase(std::remove(attachments->podIds.begin(), attachments->podIds.end(), podId),
                                       attachments->podIds.end());
        }
    }
    Refresh();
}

void BehaviorAttachmentPanel::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel_.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    if (selectedEntity_ == entt::null) {
        noSelectionLabel_.setBounds(area.removeFromTop(20));
        return;
    }

    auto pickerRow = area.removeFromTop(26);
    attachButton_.setBounds(pickerRow.removeFromRight(70).reduced(2));
    pickerRow.removeFromRight(4);
    availableBehaviors_.setBounds(pickerRow);
    area.removeFromTop(8);

    for (auto* row : rows_) {
        row->setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
    }
}

void BehaviorAttachmentPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

} // namespace ce
