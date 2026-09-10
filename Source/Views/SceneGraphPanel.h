#pragma once

#include <JuceHeader.h>

#include "engine/world.h"

namespace ce::views
{
class SceneGraphPanel final : public juce::Component
{
public:
    explicit SceneGraphPanel(engine::World& world);
    ~SceneGraphPanel() override;

    std::function<void(entt::entity)> onEntitySelected;

    void SetSelectedEntity(entt::entity entity);
    void Refresh();
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Item;
    void rebuild();

    engine::World& world_;
    juce::TreeView tree_;
    std::unique_ptr<Item> root_;
    entt::entity selected_ = entt::null;
    std::uint64_t lastSignature_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneGraphPanel)
};
} // namespace ce::views
