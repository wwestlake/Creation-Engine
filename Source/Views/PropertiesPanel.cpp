#include "Views/PropertiesPanel.h"

#include <mutex>

#include "Scene/Components.h"

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
        const int totalHeight = transformPanel_.PreferredHeight() + kSectionGap + materialsPanel_.PreferredHeight() +
                                 kSectionGap + physicsPanel_.PreferredHeight() + kSectionGap +
                                 behaviorAttachmentPanel_.PreferredHeight();
        setSize(width, totalHeight);
        resized();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        transformPanel_.setBounds(bounds.removeFromTop(transformPanel_.PreferredHeight()));
        bounds.removeFromTop(kSectionGap);
        materialsPanel_.setBounds(bounds.removeFromTop(materialsPanel_.PreferredHeight()));
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

    openEditorButton_.onClick = [this] { if (selectedEntity_ != entt::null && onOpenEditorRequested) onOpenEditorRequested(selectedEntity_); };
    openEditorButton_.setEnabled(false);
    openEditorButton_.setTooltip("Open this object's attached Pod editor -- creates and attaches one first if it has none yet.");
    addAndMakeVisible(openEditorButton_);

    playerStartCharacterLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(playerStartCharacterLabel_);
    playerStartCharacterBox_.onChange = [this] {
        SetSelectedPlayerStartCharacter(playerStartCharacterBox_.getSelectedId() > 1
            ? playerStartCharacterBox_.getText() : juce::String{});
    };
    playerStartCharacterBox_.setTooltip("The character asset instantiated and possessed when Play starts.");
    addAndMakeVisible(playerStartCharacterBox_);

    content_ = std::make_unique<ContentHost>(transformPanel_, materialsPanel_, physicsPanel_, behaviorAttachmentPanel_);
    addAndMakeVisible(scrollView_);
    scrollView_.setViewedComponent(content_.get(), false);
    scrollView_.setScrollBarsShown(true, false);
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::SetSelectedEntity(entt::entity entity) {
    const bool selectionChanged = selectedEntity_ != entity;
    selectedEntity_ = entity;
    deleteObjectButton_.setEnabled(entity != entt::null);
    openEditorButton_.setEnabled(entity != entt::null);
    transformPanel_.SetSelectedEntity(entity);
    materialsPanel_.SetSelectedEntity(entity);
    physicsPanel_.SetSelectedEntity(entity);
    behaviorAttachmentPanel_.SetSelectedEntity(entity);
    UpdatePlayerStartState(selectionChanged);
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

void PropertiesPanel::SetSelectedPlayerStartCharacter(const juce::String& assetId)
{
    if (selectedEntity_ == entt::null)
        return;

    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_))
            return;
        auto* playerStart = registry.try_get<scene::PossessionSpawn>(selectedEntity_);
        if (playerStart == nullptr || !registry.all_of<scene::SceneBuiltIn>(selectedEntity_)
            || registry.get<scene::SceneBuiltIn>(selectedEntity_).kind != scene::BuiltInKind::playerStart)
            return;
        playerStart->characterAssetId = assetId;
    }

    UpdatePlayerStartState();
}

void PropertiesPanel::UpdatePlayerStartState(bool reloadCharacterChoices)
{
    bool isPlayerStart = false;
    juce::String selectedAssetId;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        const auto& registry = world_.Registry();
        if (selectedEntity_ != entt::null && registry.valid(selectedEntity_))
        {
            if (const auto* builtIn = registry.try_get<scene::SceneBuiltIn>(selectedEntity_);
                builtIn != nullptr && builtIn->kind == scene::BuiltInKind::playerStart)
            {
                isPlayerStart = true;
                if (const auto* spawn = registry.try_get<scene::PossessionSpawn>(selectedEntity_))
                    selectedAssetId = spawn->characterAssetId;
            }
        }
    }
    playerStartCharacterLabel_.setVisible(isPlayerStart);
    playerStartCharacterBox_.setVisible(isPlayerStart);
    if (!isPlayerStart)
    {
        displayedPlayerStart_ = entt::null;
        displayedPlayerCharacterAssetId_.clear();
        return;
    }

    if (reloadCharacterChoices)
        cachedPlayerCharacterChoices_ = playerCharacterAssetChoices ? playerCharacterAssetChoices() : juce::StringArray{};

    // Avoid clearing/repopulating the ComboBox every inspector tick. Apart
    // from being needless UI churn, the old implementation performed a
    // synchronous VFS request through playerCharacterAssetChoices here.
    if (displayedPlayerStart_ == selectedEntity_ && displayedPlayerCharacterAssetId_ == selectedAssetId)
        return;

    playerStartCharacterBox_.clear(juce::dontSendNotification);
    playerStartCharacterBox_.addItem("None", 1);
    for (int index = 0; index < cachedPlayerCharacterChoices_.size(); ++index)
        playerStartCharacterBox_.addItem(cachedPlayerCharacterChoices_[index], index + 2);
    const int selectedIndex = cachedPlayerCharacterChoices_.indexOf(selectedAssetId);
    playerStartCharacterBox_.setSelectedId(selectedIndex >= 0 ? selectedIndex + 2 : 1, juce::dontSendNotification);
    displayedPlayerStart_ = selectedEntity_;
    displayedPlayerCharacterAssetId_ = selectedAssetId;
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
    auto buttonRow = bounds.removeFromTop(28);
    const auto half = buttonRow.getWidth() / 2;
    deleteObjectButton_.setBounds(buttonRow.removeFromLeft(half).reduced(4, 2));
    openEditorButton_.setBounds(buttonRow.reduced(4, 2));
    auto assignmentRow = bounds.removeFromTop(28);
    playerStartCharacterLabel_.setBounds(assignmentRow.removeFromLeft(120).reduced(4, 2));
    playerStartCharacterBox_.setBounds(assignmentRow.reduced(4, 2));
    scrollView_.setBounds(bounds);
    content_->UpdateLayout(scrollView_.getMaximumVisibleWidth());
}

} // namespace ce
