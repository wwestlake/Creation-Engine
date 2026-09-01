#include "Views/ExplorerPanel.h"

namespace ce::views
{

// Invisible container so the tree can show multiple top-level Game rows
// (juce::TreeView needs exactly one root item; this one is never painted
// and never shown -- see setRootItemVisible(false) in the constructor).
class ExplorerPanel::RootItem final : public juce::TreeViewItem
{
public:
    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "root"; }
};

class ExplorerPanel::SceneItem final : public juce::TreeViewItem
{
public:
    SceneItem(ExplorerPanel& owner, juce::String gameId, project::SceneDocumentInfo scene)
        : owner_(owner), gameId_(std::move(gameId)), scene_(std::move(scene)) {}

    bool mightContainSubItems() override { return false; }
    juce::String getUniqueName() const override { return "scene:" + gameId_ + ":" + scene_.id; }
    const juce::String& sceneId() const { return scene_.id; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        g.setColour(juce::Colour(0xffcdd6e0));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(scene_.name, 4, 0, width - 4, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged(bool isNowSelected) override
    {
        if (isNowSelected && owner_.onSceneSelected) owner_.onSceneSelected(scene_.id);
    }

private:
    ExplorerPanel& owner_;
    juce::String gameId_;
    project::SceneDocumentInfo scene_;
};

class ExplorerPanel::GameItem final : public juce::TreeViewItem
{
public:
    GameItem(ExplorerPanel& owner, project::GameDocumentInfo game) : owner_(owner), game_(std::move(game)) {}

    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "game:" + game_.id; }
    const juce::String& gameId() const { return game_.id; }
    const project::GameDocumentInfo& game() const { return game_; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(14.0f)).boldened());
        g.drawText(game_.name, 4, 0, width - 4, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged(bool isNowSelected) override
    {
        if (isNowSelected && owner_.onGameSelected) owner_.onGameSelected(game_.id);
    }

private:
    ExplorerPanel& owner_;
    project::GameDocumentInfo game_;
};

ExplorerPanel::ExplorerPanel()
{
    addAndMakeVisible(titleLabel_);
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);

    root_ = std::make_unique<RootItem>();
    tree_.setRootItem(root_.get());
    tree_.setRootItemVisible(false);
    tree_.setDefaultOpenness(false);
    tree_.setIndentSize(16);
    tree_.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff151b24));
    addAndMakeVisible(tree_);

    addAndMakeVisible(newGameButton_);
    addAndMakeVisible(newSceneButton_);
    addAndMakeVisible(saveButton_);
    addAndMakeVisible(status_);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff9fb1c7));
    status_.setJustificationType(juce::Justification::centredLeft);

    newGameButton_.onClick = [this] { if (onCreateGameRequested) onCreateGameRequested(); };
    newSceneButton_.onClick = [this] { if (onCreateSceneRequested) onCreateSceneRequested(); };
    saveButton_.onClick = [this] { if (onSaveRequested) onSaveRequested(); };
}

ExplorerPanel::~ExplorerPanel()
{
    // root_'s destructor deletes every GameItem/SceneItem beneath it; drop
    // the tree's pointer to it first so nothing touches those items mid-teardown.
    tree_.setRootItem(nullptr);
}

void ExplorerPanel::setDocuments(const juce::Array<project::GameDocumentInfo>& games,
                                 const juce::String& activeGameId, const juce::String& activeSceneId)
{
    root_->clearSubItems();
    for (const auto& game : games)
    {
        auto* gameItem = new GameItem(*this, game);
        root_->addSubItem(gameItem);
        for (const auto& scene : game.scenes)
            gameItem->addSubItem(new SceneItem(*this, game.id, scene));

        if (game.id == activeGameId)
        {
            gameItem->setOpen(true);
            for (int index = 0; index < gameItem->getNumSubItems(); ++index)
                if (auto* sceneItem = dynamic_cast<SceneItem*>(gameItem->getSubItem(index)))
                    if (sceneItem->sceneId() == activeSceneId)
                        sceneItem->setSelected(true, true, juce::dontSendNotification);
        }
    }
}

void ExplorerPanel::setStatus(const juce::String& status) { status_.setText(status, juce::dontSendNotification); }

void ExplorerPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    titleLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    auto buttons = area.removeFromBottom(28);
    area.removeFromBottom(4);
    status_.setBounds(area.removeFromBottom(36));
    area.removeFromBottom(4);
    tree_.setBounds(area);

    newGameButton_.setBounds(buttons.removeFromLeft(82));
    buttons.removeFromLeft(6);
    newSceneButton_.setBounds(buttons.removeFromLeft(86));
    buttons.removeFromLeft(6);
    saveButton_.setBounds(buttons.removeFromLeft(58));
}

void ExplorerPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff151b24));
}

} // namespace ce::views
