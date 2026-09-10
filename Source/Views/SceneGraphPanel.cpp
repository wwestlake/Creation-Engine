#include "Views/SceneGraphPanel.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Scene/Components.h"

namespace ce::views
{
namespace
{
struct Entry
{
    entt::entity entity = entt::null;
    entt::entity parent = entt::null;
    juce::String label;
    std::uint64_t signature = 0;
};

juce::String labelFor(const entt::registry& registry, entt::entity entity)
{
    juce::String label;
    if (const auto* name = registry.try_get<const scene::Name>(entity))
        label = name->value;
    if (label.isEmpty())
        label = "Unnamed Object";
    if (const auto* builtIn = registry.try_get<const scene::SceneBuiltIn>(entity))
    {
        switch (builtIn->kind)
        {
            case scene::BuiltInKind::playerStart: label = "Player Start: Player 1"; break;
            case scene::BuiltInKind::spawner: label = "Spawner: " + label; break;
            case scene::BuiltInKind::cameraMarker: label = "Camera: " + label; break;
            case scene::BuiltInKind::triggerVolume: label = "Trigger: " + label; break;
            case scene::BuiltInKind::waypoint: label = "Waypoint: " + label; break;
            case scene::BuiltInKind::audioEmitter: label = "Audio: " + label; break;
        }
    }
    return label;
}
} // namespace

class SceneGraphPanel::Item final : public juce::TreeViewItem
{
public:
    Item(SceneGraphPanel& owner, entt::entity entity, juce::String label)
        : owner_(owner), entity_(entity), label_(std::move(label)) {}

    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return juce::String(static_cast<int>(entt::to_integral(entity_))); }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        const bool isCurrentSelection = owner_.selected_ == entity_;
        if (isCurrentSelection)
            g.fillAll(juce::Colour(0xff315f8f));
        g.setColour(isCurrentSelection ? juce::Colours::white : juce::Colours::lightgrey);
        g.drawText(label_, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
    }

    void itemClicked(const juce::MouseEvent&) override
    {
        owner_.selected_ = entity_;
        owner_.tree_.repaint();
        if (owner_.onEntitySelected)
            owner_.onEntitySelected(entity_);
    }

private:
    SceneGraphPanel& owner_;
    entt::entity entity_;
    juce::String label_;
};

SceneGraphPanel::SceneGraphPanel(engine::World& world) : world_(world)
{
    tree_.setRootItemVisible(false);
    tree_.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff15181d));
    addAndMakeVisible(tree_);
    rebuild();
}

SceneGraphPanel::~SceneGraphPanel()
{
    tree_.setRootItem(nullptr);
}

void SceneGraphPanel::SetSelectedEntity(entt::entity entity)
{
    selected_ = entity;
    tree_.repaint();
}

void SceneGraphPanel::Refresh()
{
    std::uint64_t signature = 1469598103934665603ull;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        for (const auto entity : registry.storage<entt::entity>())
        {
            if (registry.all_of<scene::SceneRoot>(entity))
                continue;
            signature ^= static_cast<std::uint64_t>(entt::to_integral(entity));
            signature *= 1099511628211ull;
            if (const auto* parent = registry.try_get<const scene::Parent>(entity))
            {
                signature ^= static_cast<std::uint64_t>(entt::to_integral(parent->value));
                signature *= 1099511628211ull;
            }
        }
    }
    if (signature != lastSignature_)
    {
        lastSignature_ = signature;
        rebuild();
    }
}

void SceneGraphPanel::rebuild()
{
    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        for (const auto entity : registry.storage<entt::entity>())
        {
            if (registry.all_of<scene::SceneRoot>(entity))
                continue;
            Entry entry;
            entry.entity = entity;
            entry.label = labelFor(registry, entity);
            if (const auto* parent = registry.try_get<const scene::Parent>(entity))
                entry.parent = parent->value;
            entries.push_back(std::move(entry));
        }
    }

    auto root = std::make_unique<Item>(*this, entt::null, "Scene");
    std::unordered_map<std::uint32_t, std::vector<const Entry*>> children;
    std::unordered_map<std::uint32_t, const Entry*> known;
    for (const auto& entry : entries)
        known[static_cast<std::uint32_t>(entt::to_integral(entry.entity))] = &entry;
    for (const auto& entry : entries)
    {
        const auto parentKey = static_cast<std::uint32_t>(entt::to_integral(entry.parent));
        const bool hasRealParent = entry.parent != entt::null && known.contains(parentKey);
        children[hasRealParent ? parentKey : 0].push_back(&entry);
    }

    std::function<void(juce::TreeViewItem&, std::uint32_t)> addChildren;
    std::unordered_set<std::uint32_t> expanded;
    addChildren = [&](juce::TreeViewItem& parent, std::uint32_t parentKey)
    {
        auto& list = children[parentKey];
        std::sort(list.begin(), list.end(), [](const Entry* a, const Entry* b) { return a->label < b->label; });
        for (const auto* entry : list)
        {
            auto* item = new Item(*this, entry->entity, entry->label);
            parent.addSubItem(item);
            const auto childKey = static_cast<std::uint32_t>(entt::to_integral(entry->entity));
            if (expanded.insert(childKey).second)
                addChildren(*item, childKey);
            item->setOpen(true);
        }
    };
    expanded.insert(0);
    addChildren(*root, 0);
    root->setOpen(true);
    tree_.setRootItem(nullptr);
    root_ = std::move(root);
    tree_.setRootItem(root_.get());
}

void SceneGraphPanel::resized()
{
    tree_.setBounds(getLocalBounds());
}

void SceneGraphPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15181d));
}
} // namespace ce::views
