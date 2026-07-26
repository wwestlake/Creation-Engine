#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <JuceHeader.h>

#include "engine/world.h"

namespace ce {

// Scene hierarchy: a tree of every entity in the World that has a
// scene::Name component, nested per scene::Parent (spec section 3.2:
// "organize placed objects into a scene hierarchy/grouping structure").
// scene::Folder entities are pure organizational nodes — Name + optional
// Parent, no Transform/MeshRenderer, never drawn — created via the
// "+ Folder" button.
//
// Each row has its own Visible/Lock toggle buttons (scene::SceneFlags)
// and can be dragged onto another row to reparent it: drop onto a folder
// to become its child, onto a leaf entity to become that entity's
// sibling, or onto empty space below the tree to move to the root.
// Locked entities refuse to be reparented — "locked" meaning "can't be
// changed in the editor" has to mean *something* concrete before SC4's
// transform editing exists to gate too, and reparenting is the one edit
// this milestone actually has.
//
// Refresh() reads world_.Registry() from the message thread while
// ViewportComponent's render thread concurrently iterates/mutates the
// same registry every frame — takes world_.RegistryMutex() for exactly
// that reason (see World's own header comment for why this lock exists
// and who else has to use it). MainComponent calls Refresh() on every
// 30Hz timer tick (so it notices entities appearing/changing from
// elsewhere), so the tree is only actually torn down and rebuilt when
// the snapshot read this call differs from the previous one — rebuilding
// unconditionally every tick was measured to occasionally destroy and
// recreate the TreeViewItem/RowComponent a click had just landed on,
// silently eating the click. Open/selected state is still captured
// before a real rebuild and reapplied after by matching entt::entity
// ids, so expanding a folder doesn't fight the next refresh tick.
class HierarchyPanel final : public juce::Component,
                              private juce::DragAndDropContainer {
public:
    explicit HierarchyPanel(engine::World& world);
    ~HierarchyPanel() override;

    std::function<void(entt::entity)> onSelectionChanged;

    void Refresh();
    entt::entity SelectedEntity() const { return selectedEntity_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class EntityTreeItem;
    class RowComponent;
    friend class EntityTreeItem;
    friend class RowComponent;

    // One row's worth of state as read from the registry, snapshotted
    // under the lock each Refresh() call — also doubles as a cheap
    // "did anything actually change" comparison against the previous
    // snapshot, so Refresh() can skip the teardown-and-rebuild when
    // nothing did (see class comment above for why that matters).
    struct NodeInfo {
        entt::entity entity;
        juce::String name;
        bool isFolder;
        bool visible;
        bool locked;
        entt::entity parent;

        bool operator==(const NodeInfo& other) const {
            return entity == other.entity && name == other.name && isFolder == other.isFolder &&
                   visible == other.visible && locked == other.locked && parent == other.parent;
        }
        bool operator!=(const NodeInfo& other) const { return !(*this == other); }
    };

    void NotifySelected(entt::entity entity);
    void CreateFolder();

    // Locked entities refuse to move. Returns false (and logs why) if
    // reparenting would lock the tree into a cycle (dragging a folder
    // onto its own descendant).
    bool Reparent(entt::entity entity, entt::entity newParent);

    void SetVisible(entt::entity entity, bool visible);
    void SetLocked(entt::entity entity, bool locked);

    engine::World& world_;
    juce::Label titleLabel_{ {}, "Hierarchy" };
    juce::TextButton addFolderButton_{ "+ Folder" };
    juce::TreeView treeView_;
    std::unique_ptr<juce::TreeViewItem> rootItem_;

    entt::entity selectedEntity_ = entt::null;
    int nextFolderNumber_ = 1;
    std::vector<NodeInfo> lastSnapshot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HierarchyPanel)
};

} // namespace ce
