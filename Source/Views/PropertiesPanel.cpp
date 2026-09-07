#include "Views/PropertiesPanel.h"

#include <mutex>

namespace ce {

namespace {
constexpr int kSectionGap = 10;
}

class PropertiesPanel::ContentHost final : public juce::Component {
public:
    ContentHost(TransformPanel& transformPanel, MaterialsPanel& materialsPanel, PhysicsPropertiesPanel& physicsPanel,
                BehaviorAttachmentPanel& behaviorAttachmentPanel)
        : transformPanel_(transformPanel), materialsPanel_(materialsPanel), physicsPanel_(physicsPanel),
          behaviorAttachmentPanel_(behaviorAttachmentPanel) {
        addAndMakeVisible(transformPanel_);
        addAndMakeVisible(materialsPanel_);
        addAndMakeVisible(physicsPanel_);
        addAndMakeVisible(behaviorAttachmentPanel_);
    }

    void UpdateLayout(int width) {
        const int totalHeight = TransformPanel::kPreferredHeight + kSectionGap + MaterialsPanel::kPreferredHeight +
                                 kSectionGap + physicsPanel_.PreferredHeight() + kSectionGap +
                                 behaviorAttachmentPanel_.PreferredHeight();
        setSize(width, totalHeight);
        resized();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        transformPanel_.setBounds(bounds.removeFromTop(TransformPanel::kPreferredHeight));
        bounds.removeFromTop(kSectionGap);
        materialsPanel_.setBounds(bounds.removeFromTop(MaterialsPanel::kPreferredHeight));
        bounds.removeFromTop(kSectionGap);
        physicsPanel_.setBounds(bounds.removeFromTop(physicsPanel_.PreferredHeight()));
        bounds.removeFromTop(kSectionGap);
        behaviorAttachmentPanel_.setBounds(bounds.removeFromTop(behaviorAttachmentPanel_.PreferredHeight()));
    }

private:
    TransformPanel& transformPanel_;
    MaterialsPanel& materialsPanel_;
    PhysicsPropertiesPanel& physicsPanel_;
    BehaviorAttachmentPanel& behaviorAttachmentPanel_;
};

PropertiesPanel::PropertiesPanel(engine::World& world, interaction::EditorInteraction& interactions,
                                 frust::EngineFrustHost& frustHost, frust::PodCatalog& catalog)
    : world_(world), transformPanel_(world, interactions), materialsPanel_(world), physicsPanel_(world),
      behaviorAttachmentPanel_(world, frustHost, catalog) {
    deleteObjectButton_.onClick = [this] { DeleteSelectedEntity(); };
    deleteObjectButton_.setEnabled(false);
    addAndMakeVisible(deleteObjectButton_);

    content_ = std::make_unique<ContentHost>(transformPanel_, materialsPanel_, physicsPanel_, behaviorAttachmentPanel_);
    addAndMakeVisible(scrollView_);
    scrollView_.setViewedComponent(content_.get(), false);
    scrollView_.setScrollBarsShown(true, false);
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
    deleteObjectButton_.setEnabled(entity != entt::null);
    transformPanel_.SetSelectedEntity(entity);
    materialsPanel_.SetSelectedEntity(entity);
    physicsPanel_.SetSelectedEntity(entity);
    behaviorAttachmentPanel_.SetSelectedEntity(entity);
    resized();
}

void PropertiesPanel::DeleteSelectedEntity() {
    if (selectedEntity_ == entt::null) return;

    const auto entity = selectedEntity_;
    if (onEntityDestroying) onEntityDestroying(entity);
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(entity)) registry.destroy(entity);
    }
    SetSelectedEntity(entt::null);
}

void PropertiesPanel::Refresh() {
    transformPanel_.Refresh();
    materialsPanel_.Refresh();
    physicsPanel_.Refresh();
    behaviorAttachmentPanel_.Refresh();
    // Preferred heights can change tick to tick (Physics's shape-specific
    // fields, the attached-Behaviors row count) -- re-layout every refresh,
    // not just on selection change.
    resized();
}

void PropertiesPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void PropertiesPanel::resized() {
    auto bounds = getLocalBounds();
    deleteObjectButton_.setBounds(bounds.removeFromTop(28).reduced(4, 2));
    scrollView_.setBounds(bounds);
    content_->UpdateLayout(scrollView_.getMaximumVisibleWidth());
}

} // namespace ce
