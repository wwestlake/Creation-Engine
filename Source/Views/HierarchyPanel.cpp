#include "Views/HierarchyPanel.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Scene/AssetPlacement.h"
#include "Scene/Components.h"

namespace ce {

// Custom row: a name label plus per-row Visible/Lock toggle buttons.
// Clicks that land on this component but not on either button still reach
// TreeView's own selection handling (see the addMouseListener call in
// juce_TreeView.cpp's ItemComponent — it listens on the row component with
// wantsEventsForAllNestedChildComponents=false, which is exactly "top-level
// clicks on the row bubble up, clicks consumed by a child Button don't").
// Defined before EntityTreeItem below since createItemComponent() needs a
// complete RowComponent type at the point it's used, not just the forward
// declaration from the header.
class HierarchyPanel::RowComponent final : public juce::Component {
public:
    RowComponent(HierarchyPanel& owner, entt::entity entity, const juce::String& name, bool isFolder, bool visible,
                 bool locked)
        : owner_(owner), entity_(entity) {
        nameLabel_.setText(name, juce::dontSendNotification);
        nameLabel_.setInterceptsMouseClicks(false, false);
        nameLabel_.setColour(juce::Label::textColourId,
                              locked ? juce::Colour(0xff6a7484) : juce::Colour(0xffb8c4d5));
        if (isFolder) {
            nameLabel_.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        } else {
            nameLabel_.setFont(juce::Font(juce::FontOptions(14.0f)));
        }
        addAndMakeVisible(nameLabel_);

        visibleButton_.setButtonText(visible ? "V" : "H");
        visibleButton_.setTooltip(visible ? "Visible - click to hide" : "Hidden - click to show");
        visibleButton_.onClick = [this] {
            const bool newVisible = visibleButton_.getButtonText() == "H";
            owner_.SetVisible(entity_, newVisible);
            visibleButton_.setButtonText(newVisible ? "V" : "H");
            visibleButton_.setTooltip(newVisible ? "Visible - click to hide" : "Hidden - click to show");
        };
        addAndMakeVisible(visibleButton_);

        lockButton_.setButtonText(locked ? "L" : "U");
        lockButton_.setTooltip(locked ? "Locked - click to unlock" : "Unlocked - click to lock");
        lockButton_.onClick = [this] {
            const bool newLocked = lockButton_.getButtonText() == "U";
            owner_.SetLocked(entity_, newLocked);
            lockButton_.setButtonText(newLocked ? "L" : "U");
            lockButton_.setTooltip(newLocked ? "Locked - click to unlock" : "Unlocked - click to lock");
        };
        addAndMakeVisible(lockButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        lockButton_.setBounds(bounds.removeFromRight(22).reduced(2));
        visibleButton_.setBounds(bounds.removeFromRight(22).reduced(2));
        nameLabel_.setBounds(bounds);
    }

private:
    HierarchyPanel& owner_;
    entt::entity entity_;
    juce::Label nameLabel_;
    juce::TextButton visibleButton_;
    juce::TextButton lockButton_;
};

// One row in the tree: either a real entity (leaf, has Transform+MeshRenderer
// in the common case) or a scene::Folder (pure grouping node). Everything
// this needs about the entity is snapshotted at construction time by
// Refresh() under the registry lock, so paint/layout never has to touch the
// registry themselves — only the actions (select, reparent, toggle) call
// back into HierarchyPanel, which takes its own lock per call.
class HierarchyPanel::EntityTreeItem final : public juce::TreeViewItem {
public:
    EntityTreeItem(HierarchyPanel& owner, entt::entity entity, bool isFolder, juce::String name, bool visible,
                   bool locked, bool hasChildren)
        : owner_(owner),
          entity_(entity),
          isFolder_(isFolder),
          name_(std::move(name)),
          visible_(visible),
          locked_(locked),
          hasChildren_(hasChildren) {}

    entt::entity EntityId() const { return entity_; }

    bool mightContainSubItems() override { return hasChildren_; }

    std::unique_ptr<juce::Component> createItemComponent() override {
        return std::make_unique<RowComponent>(owner_, entity_, name_, isFolder_, visible_, locked_);
    }

    // Without this, JUCE's own doc comment on the base declaration says a
    // custom item component "consume[s] all mouse clicks" — verified the
    // hard way: without this override, clicking a row did nothing at all
    // (no selection, no drag) because TreeView only wires up its built-in
    // mouse handling (selection, itemClicked, drag-and-drop) when this
    // returns true. Button clicks inside RowComponent still work
    // normally alongside it; this only adds a listener, it doesn't divert
    // the click away from its actual target.
    bool customComponentUsesTreeViewMouseHandler() const override { return true; }

    void itemSelectionChanged(bool isNowSelected) override {
        if (isNowSelected) {
            owner_.NotifySelected(entity_);
        }
    }

    // Root (entity_ == null) and locked entities can't be dragged at all —
    // an empty var here tells JUCE there's nothing to start dragging.
    juce::var getDragSourceDescription() override {
        if (entity_ == entt::null || locked_) {
            return {};
        }
        return juce::var(static_cast<juce::int64>(entt::to_integral(entity_)));
    }

    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override {
        return details.description.isInt() || details.description.isInt64();
    }

    // Dropping onto a folder makes it that folder's child; dropping onto a
    // plain entity makes it a *sibling* of that entity (joins its current
    // parent) rather than nesting inside a leaf, since leaves can't
    // meaningfully contain other rows. The sibling lookup takes its own
    // short lock — separate from Reparent()'s own lock below it, since
    // world_.RegistryMutex() is a plain std::mutex and re-locking it on the
    // same thread would deadlock, not just block.
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details, int) override {
        if (!(details.description.isInt() || details.description.isInt64())) {
            return;
        }
        const auto raw =
            static_cast<std::underlying_type_t<entt::entity>>(static_cast<juce::int64>(details.description));
        const entt::entity dragged = static_cast<entt::entity>(raw);

        entt::entity targetParent = entity_;
        if (!isFolder_) {
            std::lock_guard<std::mutex> lock(owner_.world_.RegistryMutex());
            if (const auto* parent = owner_.world_.Registry().try_get<scene::Parent>(entity_)) {
                targetParent = parent->value;
            } else {
                targetParent = entt::null;
            }
        }

        if (owner_.Reparent(dragged, targetParent)) {
            owner_.Refresh();
        }
    }

private:
    HierarchyPanel& owner_;
    entt::entity entity_;
    bool isFolder_;
    juce::String name_;
    bool visible_;
    bool locked_;
    bool hasChildren_;
};

HierarchyPanel::HierarchyPanel(engine::World& world, ViewportComponent& viewport)
    : world_(world), viewport_(viewport) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    addFolderButton_.onClick = [this] { CreateFolder(); };
    addAndMakeVisible(addFolderButton_);

    addAssetButton_.onClick = [this] {
        juce::PopupMenu menu;
        for (const auto& name : viewport_.Catalog().Names()) {
            menu.addItem(name, [this, name] { AddEntity(name); });
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addAssetButton_));
    };
    addAndMakeVisible(addAssetButton_);

    duplicateButton_.onClick = [this] { DuplicateSelected(); };
    addAndMakeVisible(duplicateButton_);

    deleteButton_.onClick = [this] { DeleteSelected(); };
    addAndMakeVisible(deleteButton_);

    treeView_.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff10141a));
    treeView_.setRootItemVisible(false);
    treeView_.setDefaultOpenness(true);
    treeView_.setMultiSelectEnabled(false);
    treeView_.setIndentSize(16);
    addAndMakeVisible(treeView_);

    rootItem_ = std::make_unique<EntityTreeItem>(*this, entt::null, true, "Root", true, false, true);
    treeView_.setRootItem(rootItem_.get());

    UpdateActionButtonState();
}

HierarchyPanel::~HierarchyPanel() {
    // rootItem_ (a member declared after treeView_) is destroyed before
    // treeView_ is; without this, treeView_ would briefly hold a dangling
    // raw pointer to it during teardown.
    treeView_.setRootItem(nullptr);
}

void HierarchyPanel::NotifySelected(entt::entity entity) {
    selectedEntity_ = entity;
    UpdateActionButtonState();
    if (onSelectionChanged) {
        onSelectionChanged(selectedEntity_);
    }
}

void HierarchyPanel::UpdateActionButtonState() {
    const bool hasSelection = selectedEntity_ != entt::null;
    duplicateButton_.setEnabled(hasSelection);
    deleteButton_.setEnabled(hasSelection);
}

void HierarchyPanel::CreateFolder() {
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();

        entt::entity parent = entt::null;
        if (selectedEntity_ != entt::null && registry.valid(selectedEntity_) &&
            registry.all_of<scene::Folder>(selectedEntity_)) {
            parent = selectedEntity_;
        }

        const auto entity = world_.CreateEntity();
        registry.emplace<scene::Name>(entity, scene::Name{ "Folder " + juce::String(nextFolderNumber_++) });
        registry.emplace<scene::Folder>(entity);
        registry.emplace<scene::SceneFlags>(entity, scene::SceneFlags{});
        registry.emplace<scene::Parent>(entity, scene::Parent{ parent });
    }
    Refresh();
}

void HierarchyPanel::AddEntity(const juce::String& assetName) {
    const auto asset = viewport_.Catalog().Find(assetName);
    if (asset.mesh == nullptr) {
        return;
    }
    const auto spawnPosition = viewport_.SpawnPosition();

    entt::entity newEntity = entt::null;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();

        entt::entity parent = entt::null;
        if (selectedEntity_ != entt::null && registry.valid(selectedEntity_) &&
            registry.all_of<scene::Folder>(selectedEntity_)) {
            parent = selectedEntity_;
        }

        // See Scene/AssetPlacement.h for the shared component list this
        // now builds (also used by importers that auto-place a dropped
        // model) -- the animator's "starts paused, first clip active"
        // choice lives there so a freshly-placed animated asset has
        // something to preview immediately regardless of which caller
        // placed it.
        newEntity = scene::PlaceAssetEntity(world_, asset, assetName + " " + juce::String(nextEntityNumber_++),
                                             scene::ToVec3(spawnPosition), parent);
    }

    // Select before Refresh(), not after: Refresh()'s rebuild re-selects
    // whatever selectedEntity_ already is by matching entity ids, so this
    // ordering is what makes the new row actually show highlighted
    // instead of just being the (invisibly) selected entity.
    NotifySelected(newEntity);
    Refresh();
}

void HierarchyPanel::DuplicateSelected() {
    if (selectedEntity_ == entt::null) {
        return;
    }

    entt::entity newEntity = entt::null;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            return;
        }

        const auto* name = registry.try_get<scene::Name>(selectedEntity_);
        const auto* transform = registry.try_get<scene::Transform>(selectedEntity_);
        const auto* meshRenderer = registry.try_get<scene::MeshRenderer>(selectedEntity_);
        const auto* parent = registry.try_get<scene::Parent>(selectedEntity_);
        const bool isFolder = registry.all_of<scene::Folder>(selectedEntity_);

        newEntity = world_.CreateEntity();
        registry.emplace<scene::Name>(
            newEntity, scene::Name{ (name != nullptr ? name->value : juce::String("Entity")) + " copy" });
        if (transform != nullptr) {
            registry.emplace<scene::Transform>(newEntity, *transform);
        }
        if (meshRenderer != nullptr) {
            registry.emplace<scene::MeshRenderer>(newEntity, *meshRenderer);
        }
        if (isFolder) {
            registry.emplace<scene::Folder>(newEntity);
        }
        // Always unlocked/visible on the copy, even if the source is
        // locked -- duplicating doesn't modify the source, so "locked"
        // has no reason to carry over.
        registry.emplace<scene::SceneFlags>(newEntity, scene::SceneFlags{});
        registry.emplace<scene::Parent>(newEntity, scene::Parent{ parent != nullptr ? parent->value : entt::null });
    }

    NotifySelected(newEntity);
    Refresh();
}

void HierarchyPanel::DeleteSelected() {
    if (selectedEntity_ == entt::null) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            return;
        }
        if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(selectedEntity_)) {
            if (sceneFlags->locked) {
                std::cout << "[scene] refused to delete a locked entity" << std::endl;
                return;
            }
        }
    }

    // Behaviors may call back into the Engine, so lifecycle hooks run before
    // taking the registry lock used by the actual destruction operation.
    if (onEntityDestroying) {
        onEntityDestroying(selectedEntity_);
    }

    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            return;
        }

        if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(selectedEntity_)) {
            if (sceneFlags->locked) {
                return;
            }
        }

        // Reparent any children up to this entity's own parent instead of
        // leaving them pointing at a destroyed entity -- deleting a
        // folder ungroups its contents rather than silently vanishing
        // them from the tree (Refresh() only ever walks children of
        // entities that still exist in `nodes`).
        entt::entity replacementParent = entt::null;
        if (const auto* parent = registry.try_get<scene::Parent>(selectedEntity_)) {
            replacementParent = parent->value;
        }
        auto childView = registry.view<scene::Parent>();
        for (auto candidate : childView) {
            if (childView.get<scene::Parent>(candidate).value == selectedEntity_) {
                childView.get<scene::Parent>(candidate).value = replacementParent;
            }
        }

        registry.destroy(selectedEntity_);
    }

    NotifySelected(entt::null);
    Refresh();
}

bool HierarchyPanel::Reparent(entt::entity entity, entt::entity newParent) {
    if (entity == entt::null || entity == newParent) {
        return false;
    }

    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    if (!registry.valid(entity)) {
        return false;
    }

    if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(entity)) {
        if (sceneFlags->locked) {
            std::cout << "[scene] refused to reparent a locked entity" << std::endl;
            return false;
        }
    }

    // Walk up from newParent looking for entity — if we find it, this move
    // would make entity an ancestor of itself.
    entt::entity walker = newParent;
    while (walker != entt::null) {
        if (walker == entity) {
            std::cout << "[scene] refused to reparent: would create a cycle" << std::endl;
            return false;
        }
        if (const auto* parent = registry.try_get<scene::Parent>(walker)) {
            walker = parent->value;
        } else {
            walker = entt::null;
        }
    }

    registry.emplace_or_replace<scene::Parent>(entity, scene::Parent{ newParent });
    return true;
}

void HierarchyPanel::SetVisible(entt::entity entity, bool visible) {
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    if (auto* sceneFlags = world_.Registry().try_get<scene::SceneFlags>(entity)) {
        sceneFlags->visible = visible;
    }
}

void HierarchyPanel::SetLocked(entt::entity entity, bool locked) {
    std::lock_guard<std::mutex> lock(world_.RegistryMutex());
    if (auto* sceneFlags = world_.Registry().try_get<scene::SceneFlags>(entity)) {
        sceneFlags->locked = locked;
    }
}

void HierarchyPanel::Refresh() {
    using RawEntity = std::underlying_type_t<entt::entity>;

    std::vector<NodeInfo> nodes;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        auto view = registry.view<const scene::Name>();
        for (auto entity : view) {
            NodeInfo info;
            info.entity = entity;
            info.name = view.get<const scene::Name>(entity).value;
            info.isFolder = registry.all_of<scene::Folder>(entity);
            if (const auto* sceneFlags = registry.try_get<scene::SceneFlags>(entity)) {
                info.visible = sceneFlags->visible;
                info.locked = sceneFlags->locked;
            } else {
                info.visible = true;
                info.locked = false;
            }
            if (const auto* parent = registry.try_get<scene::Parent>(entity)) {
                info.parent = parent->value;
            } else {
                info.parent = entt::null;
            }
            nodes.push_back(std::move(info));
        }
    }

    // Called on every 30Hz timer tick (see class comment for why) — most
    // of those ticks see the exact same scene, so bail out before
    // touching the TreeView at all when nothing did. This is also what
    // makes it safe for something else to have clicked/dragged a row in
    // between calls: a no-op Refresh() can never race that.
    if (nodes == lastSnapshot_) {
        return;
    }
    lastSnapshot_ = nodes;

    // Capture which folders are open and what's selected before the tree
    // gets torn down and rebuilt from scratch below.
    std::vector<RawEntity> openIds;
    std::function<void(juce::TreeViewItem*)> captureOpen = [&](juce::TreeViewItem* item) {
        for (int i = 0; i < item->getNumSubItems(); ++i) {
            auto* child = item->getSubItem(i);
            if (child->isOpen()) {
                openIds.push_back(entt::to_integral(static_cast<EntityTreeItem*>(child)->EntityId()));
            }
            captureOpen(child);
        }
    };
    if (rootItem_ != nullptr) {
        captureOpen(rootItem_.get());
    }
    const entt::entity previousSelection = selectedEntity_;

    std::unordered_map<RawEntity, std::vector<const NodeInfo*>> childrenByParent;
    for (const auto& node : nodes) {
        childrenByParent[entt::to_integral(node.parent)].push_back(&node);
    }

    auto newRoot = std::make_unique<EntityTreeItem>(*this, entt::null, true, "Root", true, false, true);
    std::function<void(EntityTreeItem&, entt::entity)> build = [&](EntityTreeItem& parentItem, entt::entity parentId) {
        const auto it = childrenByParent.find(entt::to_integral(parentId));
        if (it == childrenByParent.end()) {
            return;
        }
        for (const auto* node : it->second) {
            const bool hasChildren = childrenByParent.find(entt::to_integral(node->entity)) != childrenByParent.end();
            auto* item =
                new EntityTreeItem(*this, node->entity, node->isFolder, node->name, node->visible, node->locked, hasChildren);
            parentItem.addSubItem(item);
            build(*item, node->entity);
        }
    };
    build(*newRoot, entt::null);

    treeView_.setRootItem(nullptr);
    rootItem_ = std::move(newRoot);
    treeView_.setRootItem(rootItem_.get());

    bool stillSelected = false;
    std::function<void(juce::TreeViewItem*)> restore = [&](juce::TreeViewItem* item) {
        for (int i = 0; i < item->getNumSubItems(); ++i) {
            auto* child = item->getSubItem(i);
            auto* entityItem = static_cast<EntityTreeItem*>(child);
            const auto id = entt::to_integral(entityItem->EntityId());
            if (std::find(openIds.begin(), openIds.end(), id) != openIds.end()) {
                child->setOpen(true);
            }
            if (previousSelection != entt::null && entityItem->EntityId() == previousSelection) {
                child->setSelected(true, true, juce::dontSendNotification);
                stillSelected = true;
            }
            restore(child);
        }
    };
    restore(rootItem_.get());

    if (!stillSelected && previousSelection != entt::null) {
        selectedEntity_ = entt::null;
        if (onSelectionChanged) {
            onSelectionChanged(selectedEntity_);
        }
    }
}

void HierarchyPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void HierarchyPanel::resized() {
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop(24);
    titleLabel_.setBounds(headerArea.removeFromLeft(headerArea.getWidth() - 72));
    addFolderButton_.setBounds(headerArea.reduced(2));

    area.removeFromTop(4);
    auto actionsArea = area.removeFromTop(24);
    const int third = actionsArea.getWidth() / 3;
    addAssetButton_.setBounds(actionsArea.removeFromLeft(third).reduced(2));
    duplicateButton_.setBounds(actionsArea.removeFromLeft(third).reduced(2));
    deleteButton_.setBounds(actionsArea.reduced(2));

    area.removeFromTop(4);
    treeView_.setBounds(area);
}

} // namespace ce
