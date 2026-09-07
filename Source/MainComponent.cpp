#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include <creation/services/SuiteVfsJsonStore.h>

#include <creation/ui/CreationSuiteLogos.h>
#include "Assets/AssetPackStore.h"
#include "Assets/EngineAssetPack.h"
#include "Diagnostics/EngineLog.h"
#include "Import/Importers/GltfAssetImporter.h"
#include "Scene/Components.h"
#include "Scene/EngineSceneSerializer.h"
#include "Scene/ObjectDefinitionNaming.h"
#include "Project/EngineGameDocument.h"
#include "Views/NewGameDialog.h"
#include "engine/foundation_gameplay.h"
#include "engine/tick_phase.h"

namespace {
constexpr juce::CommandID kRunGameClientCommand = 0x1001;
constexpr juce::CommandID kUndoInteractionCommand = 0x1002;
constexpr juce::CommandID kRedoInteractionCommand = 0x1003;
constexpr juce::CommandID kNewGameCommand = 0x1004;
constexpr juce::CommandID kNewSceneCommand = 0x1005;
constexpr juce::CommandID kSaveCommand = 0x1006;
constexpr juce::CommandID kImportCommand = 0x1007;
constexpr juce::CommandID kNewProjectCommand = 0x1008;
constexpr juce::CommandID kOpenProjectBrowserCommand = 0x1009;
constexpr juce::CommandID kPossessDesignerCharacterCommand = 0x100A;

// menuItemSelected's ids for the View menu's "jump to panel" entries and the
// Help menu's About box -- plain PopupMenu ids, not routed through the
// ApplicationCommandManager (there's nothing to give these a keyboard
// shortcut or an enabled/disabled state, unlike the File/Edit commands above).
constexpr int kViewPanelItemIdBase = 9000;
constexpr int kViewResetLayoutItemId = 8999;
constexpr int kHelpAboutItemId = 8998;

struct DockPanelMenuEntry { const char* id; const char* label; };
constexpr DockPanelMenuEntry kDockPanelMenuEntries[] = {
    { "viewport", "Scene Viewport" },
    { "properties", "Properties" },
    { "input-bindings", "Input Bindings" },
    { "lighting", "Lighting" },
    { "materials", "Materials" },
    { "content-browser", "Content Browser" },
    // "server"/"settings" deliberately not listed here -- both are still
    // non-functional PlaceholderPanel stubs ("coming soon"), which only
    // clutters the dock/menu right now. Not registered below either.
    // serverPanel_/settingsPanel_ themselves are untouched -- trivially
    // reversible once either is real.
    //
    // "pods"/"pod-info" also deliberately not listed here -- unlike every
    // other entry, they don't exist as dock tabs at all until a Pod is
    // open (OpenPodEditor(), called from the Content Browser), and now
    // there's one such pair per currently-open Pod rather than a single
    // fixed pair, so a permanent View-menu entry wouldn't even name the
    // right one. Open a Pod from its Content Browser row/right-click menu
    // instead; once open, its tab is right there to click on directly.
    // Pod/Asset Workflow plan Phase 5.
};

class NonOwningPanelHost final : public juce::Component
{
public:
    explicit NonOwningPanelHost(juce::Component& component) : content(component) { addAndMakeVisible(content); }
    void resized() override { content.setBounds(getLocalBounds()); }
private:
    juce::Component& content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NonOwningPanelHost)
};

}

MainComponent::MainComponent()
    : viewport_(world_, interactions_, viewportRenderHost_),
      importPanel_(world_, viewport_, projectSession_),
      djehutiImportWatcher_(world_, viewport_, objectDefinitions_),
      lightPanel_(viewport_),
      materialsPanel_(viewport_),
      contentBrowserPanel_(viewport_, importPanel_, podCatalog_, objectDefinitions_) {
    physicsWorld_.AttachToWorld(world_);
    // See suiteProcessRegistration_'s header comment: this is what keeps
    // CreationSuiteVfsService alive while this app is actually running.
    suiteProcessRegistration_.RegisterSelf("CreationEngine");
    importPanel_.SetObjectDefinitions(&objectDefinitions_);
    // ImportPanel is never mounted as a visible panel (Content Browser owns
    // import UI now) -- its own log_ would otherwise be invisible. Route
    // every import result line into the real engine log instead (visible
    // in the Log window, and persisted to the project's VFS) rather than
    // a transient status-bar line.
    importPanel_.onLogLine = [](const juce::String& line) { ce::diagnostics::EngineLog::Info("Import", line); };
    importPanel_.onContentChanged = [this] { contentBrowserPanel_.Refresh(); };
    inputBindingsPanel_.onCombosChanged = [this] { RefreshComboEventNodes(); };
    // See engineLogVfsWriter_'s own header comment for why this is a
    // one-time call, not something re-wired on every project open/close.
    engineLogVfsWriter_.SetSession(&projectSession_);

    commandManager_.registerAllCommandsForTarget(this);
    commandManager_.getKeyMappings()->addKeyPress(
        kRunGameClientCommand,
        juce::KeyPress('G', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0));
    commandManager_.getKeyMappings()->addKeyPress(kUndoInteractionCommand, juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    commandManager_.getKeyMappings()->addKeyPress(kRedoInteractionCommand, juce::KeyPress('Y', juce::ModifierKeys::ctrlModifier, 0));
    addKeyListener(commandManager_.getKeyMappings());

    // See viewportRenderHost_'s header comment: an always-alive host for
    // the 3D viewport's GL context, outside the dock tree, kept behind
    // everything and never hidden. Its bounds are synced to viewport_'s
    // own bounds by componentMovedOrResized() below.
    // A real nonzero starting size, not (0,0,0,0) -- JUCE never actually
    // creates the attached OpenGLContext against a component that starts
    // out zero-sized (confirmed by testing: newOpenGLContextCreated()
    // never fires at all, even after later resizes/repositions restore a
    // sane size). syncViewportRenderHost() corrects this to the real
    // on-screen placement as soon as the dock layout runs.
    viewportRenderHost_.setBounds(0, 0, 1280, 720);
    addAndMakeVisible(viewportRenderHost_);
    viewportRenderHost_.setInterceptsMouseClicks(false, false);
    viewportRenderHost_.toBack();
    viewport_.addComponentListener(this);

    juce::String suiteErr;
    suiteSettings_ = suiteSettingsStore_.load(suiteErr);

    std::string frustError;
    if (!frustHost_.loadBundled(frustError)) {
        juce::Logger::writeToLog("Creation Engine FRust host: " + juce::String(frustError));
    }
    objectDefinitionEditorPanel_ =
        std::make_unique<ce::views::ObjectDefinitionEditorPanel>(objectDefinitions_, podCatalog_, projectSession_);
    contentBrowserPanel_.onAssetOpened = [this](const creation::assets::AssetDescriptor& descriptor) {
        if (descriptor.kind == creation::assets::AssetKind::pod) {
            OpenPodEditor(descriptor.displayName);
        } else if (descriptor.kind == creation::assets::AssetKind::objectDefinition) {
            EnsureObjectDefinitionPanelOpen();
            objectDefinitionEditorPanel_->OpenDefinition(descriptor.displayName);
        } else if (descriptor.kind == creation::assets::AssetKind::game) {
            for (const auto& game : games_)
                if (game.catalogAssetId == descriptor.id) { selectGame(game.id); return; }
        } else if (descriptor.kind == creation::assets::AssetKind::scene) {
            // Content Browser browses every scene project-wide, not just
            // the active game's -- find which game owns this scene first,
            // switching games via selectGame (which already does the
            // save-then-load dance) before selecting the scene itself.
            for (const auto& game : games_) {
                for (const auto& scene : game.scenes) {
                    if (scene.catalogAssetId != descriptor.id) continue;
                    if (game.id != activeGame_.id) selectGame(game.id);
                    selectScene(scene.id);
                    return;
                }
            }
        }
    };
    contentBrowserPanel_.onPodCreated = [this](const juce::String& name) { OpenPodEditor(name); };
    contentBrowserPanel_.onObjectDefinitionCreated = [this](const juce::String& id) {
        EnsureObjectDefinitionPanelOpen();
        objectDefinitionEditorPanel_->OpenDefinition(id);
    };
    contentBrowserPanel_.onGameDeleteRequested = [this](const juce::String& catalogAssetId) {
        juce::String targetGameId;
        for (const auto& game : games_)
            if (game.catalogAssetId == catalogAssetId) { targetGameId = game.id; break; }
        if (targetGameId.isEmpty()) return;

        juce::String error;
        const bool wasActive = targetGameId == activeGame_.id;
        if (!ce::project::EngineGameDocumentStore::deleteGame(projectSession_, games_, targetGameId, error)) {
            headerBar_.setStatusText("Could not delete game: " + error);
            return;
        }
        contentBrowserPanel_.Refresh();
        if (wasActive && !games_.isEmpty()) selectGame(games_.getFirst().id);
    };
    contentBrowserPanel_.onSceneDeleteRequested = [this](const juce::String& catalogAssetId) {
        for (auto& game : games_) {
            juce::String targetSceneId;
            for (const auto& scene : game.scenes)
                if (scene.catalogAssetId == catalogAssetId) { targetSceneId = scene.id; break; }
            if (targetSceneId.isEmpty()) continue;

            juce::String error;
            const bool wasActive = targetSceneId == activeScene_.id && game.id == activeGame_.id;
            if (!ce::project::EngineGameDocumentStore::deleteScene(projectSession_, game, targetSceneId, error)) {
                headerBar_.setStatusText("Could not delete scene: " + error);
                return;
            }
            if (game.id == activeGame_.id) activeGame_ = game;
            contentBrowserPanel_.Refresh();
            if (wasActive) selectScene(game.entrySceneId);
            return;
        }
    };
    // Editor UI/Workflow Overhaul plan, Phase 4: Content Browser's row
    // context menu's Rename action for Game/Scene rows -- same catalogAssetId
    // -> internal-id lookup shape as onGameDeleteRequested/onSceneDeleteRequested
    // above, reusing RenameGame/RenameScene (previously only reachable from
    // the now-deleted Explorer panel).
    contentBrowserPanel_.onGameRenameRequested = [this](const juce::String& catalogAssetId) {
        for (const auto& game : games_)
            if (game.catalogAssetId == catalogAssetId) { RenameGame(game.id); return; }
    };
    contentBrowserPanel_.onSceneRenameRequested = [this](const juce::String& catalogAssetId) {
        for (const auto& game : games_)
            for (const auto& scene : game.scenes)
                if (scene.catalogAssetId == catalogAssetId) { RenameScene(game.id, scene.id); return; }
    };
    // Content Browser is the only source that produces this description
    // shape (see ContentBrowserPanel::AssetRow::mouseDrag). Placement lives
    // here, not in ViewportComponent or ContentBrowserPanel, because it
    // needs projectSession_/objectDefinitions_/suiteSettings_ together --
    // an asset browser's job ends at "this asset exists"; where it lands
    // in a scene is this class's call, not the browser's or the importer's.
    viewport_.onAssetDropped = [this](const juce::String& description, juce::Point<int> localPosition) {
        HandleAssetDropped(description, localPosition);
    };
    frustHost_.setSceneTransitionRequestHandler([this](const std::string& reference) {
        const juce::String sceneReference(reference);
        for (const auto& scene : activeGame_.scenes)
            if (scene.id == sceneReference || scene.name == sceneReference) { pendingSceneTransitionId_ = scene.id; return; }
    });
    frustHost_.setActiveGameIdProvider([this] { return activeGame_.id.toStdString(); });
    frustHost_.setActiveSceneIdProvider([this] { return activeScene_.id.toStdString(); });
    // Backs core.asset.exists (Node/Behavior Graph Foundations plan Phase
    // 8, the first real capability node) -- a real query against
    // ProjectSession's asset catalog, not a stub.
    frustHost_.setAssetExistsProvider([this](const std::string& name) {
        if (!projectSession_.isValid()) return false;
        const juce::String target(name);
        for (const auto& asset : projectSession_.getManifest().assetCatalog.query({}))
            if (asset.displayName == target) return true;
        return false;
    });
    frustHost_.setInputActionSystem(&inputActionSystem_);

    headerBar_.setAppTitle("Creation Engine");
    headerBar_.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
    headerBar_.setProjectLabel("Project: Untitled Engine");
    headerBar_.audioButton.setButtonText("Engine");
    headerBar_.tourButton.setButtonText("Tools");
    headerBar_.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::rewind, false);
    headerBar_.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::fastForward, false);
    headerBar_.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::record, false);
    headerBar_.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::loop, false);
    headerBar_.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::click, false);
    suiteShellController_.attach(headerBar_,
                                 {
                                     "Creation Engine",
                                     creation::assets::SuiteAppDomain::engine,
                                     juce::Colour(0xff15181d),
                                     creation::ui::SuiteAssetManagerCapability{ "Creation Engine", creation::assets::SuiteAppDomain::engine, { ".frust" }, { ".frust" } }
                                 },
                                 [this](const juce::String& status)
                                 {
                                     headerBar_.setStatusText(status);
                                 });
    suiteShellController_.onProjectOpenRequested = [this](const juce::String& projectId)
    {
        openProject(projectId);
    };
    headerBar_.onProjectMenuRequested = [this]
    {
        suiteShellController_.showProjectBrowser();
    };
    addAndMakeVisible(headerBar_);
    headerBar_.onPlay = [this] { SetPlaying(true); };
    headerBar_.onPause = [this] { SetPlaying(false); };
    headerBar_.onStop = [this] {
        SetPlaying(false);
        world_.ResetTick();
        if (possessedEntityId_ != -1) {
            // Possession has its own runtime state (a live Jolt
            // CharacterVirtual + the spawned entity) that doesn't belong to
            // isPlaying_/world_.ResetTick() above -- Stop is still the one
            // way out (Decision 7), it just also has this to clean up now.
            physicsWorld_.DestroyCharacter(possessedEntityId_);
            {
                std::lock_guard<std::mutex> lock(world_.RegistryMutex());
                const auto entityHandle = static_cast<entt::entity>(possessedEntityId_);
                if (world_.Registry().valid(entityHandle)) world_.Registry().destroy(entityHandle);
            }
            possessedEntityId_ = -1;
            viewport_.ExitPossessedMode();
        }
        headerBar_.setStatusText("Stopped");
    };
    headerBar_.setStatusText("Editing");

    menuBar_ = std::make_unique<juce::MenuBarComponent>(static_cast<juce::MenuBarModel*>(this));
    // No suite-wide dark LookAndFeel is set here either (see Creation
    // Station's MainComponent for the same fix/comment) -- without explicit
    // colours MenuBarComponent's default LookAndFeel_V4 scheme renders dark
    // text on a dark bar, invisible against this app's navy chrome.
    menuBar_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c2230));
    menuBar_->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a3244));
    menuBar_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    menuBar_->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(*menuBar_);
    runGameButton_.onClick = [this] { openGameClient(); };
    runGameButton_.setTooltip("Open an isolated game client window. Click again for another local multiplayer client.");
    addAndMakeVisible(runGameButton_);
    possessCharacterButton_.onClick = [this] { possessDesignerCharacter(); };
    possessCharacterButton_.setTooltip("Possess a walking character at the free-fly camera's current position and enter Play in place.");
    addAndMakeVisible(possessCharacterButton_);

    propertiesPanel_.onEntityDestroying = [this](entt::entity entity) {
        frustHost_.notifyObjectDestroyed(entity, static_cast<std::int64_t>(world_.CurrentTick()));
    };
    // Editor UI/Workflow Overhaul plan, Phase 5 (Decision 4): object-first
    // Pod creation. If the selected entity already has an attached Pod,
    // just open its editor (the first one, if it somehow has more than
    // one -- no picker UI for that yet). If it has none, create one now
    // via the same logic the Create menu uses and attach it immediately,
    // so the code is connected from the instant it exists rather than a
    // separate later step.
    propertiesPanel_.onOpenEditorRequested = [this](entt::entity entity) {
        juce::String podName;
        {
            std::lock_guard<std::mutex> lock(world_.RegistryMutex());
            auto& registry = world_.Registry();
            if (!registry.valid(entity)) return;
            if (const auto* attachments = registry.try_get<ce::scene::BehaviorAttachments>(entity);
                attachments != nullptr && !attachments->podIds.empty()) {
                podName = attachments->podIds.front();
            }
        }
        if (podName.isEmpty()) {
            podName = contentBrowserPanel_.CreateNewPod(ce::frust::PodKind::Behavior);
            if (podName.isEmpty()) return; // creation failed; already reported to the user.
            std::lock_guard<std::mutex> lock(world_.RegistryMutex());
            auto& registry = world_.Registry();
            if (!registry.valid(entity)) return;
            registry.get_or_emplace<ce::scene::BehaviorAttachments>(entity).podIds.push_back(podName);
        }
        OpenPodEditor(podName);
    };
    initialiseDockingWorkspace();

    dockManager_->activatePanel("viewport");
    SetPlaying(false);

    setSize(1400, 900);
    startTimerHz(30);

    juce::String projectError;
    if (!ensureProjectSessionActive(projectError) && projectError.isNotEmpty())
        headerBar_.setStatusText("Project setup: " + projectError);
}

MainComponent::~MainComponent() {
    removeKeyListener(commandManager_.getKeyMappings());
    stopTimer();
    gameClients_.clear();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void MainComponent::resized() {
    auto bounds = getLocalBounds();

    headerBar_.setBounds(bounds.removeFromTop(96));
    if (menuBar_ != nullptr) menuBar_->setBounds(bounds.removeFromTop(28));
    runGameButton_.setBounds(getWidth() - 170, 128, 158, 34);
    possessCharacterButton_.setBounds(getWidth() - 170 - 8 - 158, 128, 158, 34);

    if (dockManager_ != nullptr) dockManager_->setBounds(bounds);

    // Also synced here, not just from the viewport_ ComponentListener
    // callbacks above: viewport_'s own bounds/visible-flag can already be
    // in their final state by the time the real top-level window first
    // becomes visible (isShowing() only just turned true, with nothing on
    // viewport_ itself changing to fire a listener callback), which would
    // otherwise leave viewportRenderHost_ parked off-canvas forever.
    syncViewportRenderHost();
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.add(kRunGameClientCommand);
    commands.add(kUndoInteractionCommand);
    commands.add(kRedoInteractionCommand);
    commands.add(kNewGameCommand);
    commands.add(kNewSceneCommand);
    commands.add(kSaveCommand);
    commands.add(kImportCommand);
    commands.add(kNewProjectCommand);
    commands.add(kOpenProjectBrowserCommand);
    commands.add(kPossessDesignerCharacterCommand);
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    if (commandID == kRunGameClientCommand)
    {
        result.setInfo("Run Game Client", "Open an isolated game client window", "Game", {});
        result.addDefaultKeypress('G', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier);
    }
    if (commandID == kUndoInteractionCommand)
    {
        result.setInfo("Undo", "Undo the last scene interaction", "Edit", {});
        result.addDefaultKeypress('Z', juce::ModifierKeys::ctrlModifier);
    }
    if (commandID == kRedoInteractionCommand)
    {
        result.setInfo("Redo", "Redo the last scene interaction", "Edit", {});
        result.addDefaultKeypress('Y', juce::ModifierKeys::ctrlModifier);
    }
    if (commandID == kNewGameCommand)
        result.setInfo("New Game", "Add a new game to this Suite project", "File", {});
    if (commandID == kNewSceneCommand)
    {
        result.setInfo("New Scene", "Add a new scene to the active game", "File", {});
        result.setActive(!activeGame_.id.isEmpty());
    }
    if (commandID == kSaveCommand)
    {
        result.setInfo("Save", "Save the active game and scene", "File", {});
        result.addDefaultKeypress('S', juce::ModifierKeys::ctrlModifier);
    }
    if (commandID == kImportCommand)
        result.setInfo("Import...", "Open a file browser to import assets", "File", {});
    if (commandID == kNewProjectCommand)
        result.setInfo("New Project...", "Create a new Suite project for Creation Engine", "Project", {});
    if (commandID == kOpenProjectBrowserCommand)
        result.setInfo("Open Project Browser...", "Browse and switch Suite projects", "Project", {});
    if (commandID == kPossessDesignerCharacterCommand)
    {
        result.setInfo("Possess Designer Character", "Walk around as a character at the free-fly camera's position, in Play, in place", "Game", {});
        result.addDefaultKeypress('P', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier);
    }
}

bool MainComponent::perform(const juce::ApplicationCommandTarget::InvocationInfo& info)
{
    if (info.commandID == kRunGameClientCommand)
    {
        openGameClient();
        return true;
    }
    if (info.commandID == kUndoInteractionCommand) return interactions_.undo();
    if (info.commandID == kRedoInteractionCommand) return interactions_.redo();
    if (info.commandID == kNewGameCommand) { createGame(); return true; }
    if (info.commandID == kNewSceneCommand) { createScene(); return true; }
    if (info.commandID == kSaveCommand) { saveSessionToDisk(true); return true; }
    if (info.commandID == kImportCommand)
    {
        if (dockManager_ != nullptr) dockManager_->activatePanel("content-browser");
        importPanel_.BrowseAndImport();
        return true;
    }
    if (info.commandID == kNewProjectCommand) { createNewProject(); return true; }
    if (info.commandID == kOpenProjectBrowserCommand) { suiteShellController_.showProjectBrowser(); return true; }
    if (info.commandID == kPossessDesignerCharacterCommand) { possessDesignerCharacter(); return true; }
    return false;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Project", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) // File
    {
        menu.addCommandItem(&commandManager_, kNewGameCommand);
        menu.addCommandItem(&commandManager_, kNewSceneCommand);
        menu.addSeparator();
        menu.addCommandItem(&commandManager_, kSaveCommand);
        menu.addSeparator();
        menu.addCommandItem(&commandManager_, kImportCommand);
        return menu;
    }

    if (topLevelMenuIndex == 1) // Edit
    {
        menu.addCommandItem(&commandManager_, kUndoInteractionCommand);
        menu.addCommandItem(&commandManager_, kRedoInteractionCommand);
        return menu;
    }

    if (topLevelMenuIndex == 2) // View
    {
        int itemId = kViewPanelItemIdBase;
        for (const auto& entry : kDockPanelMenuEntries)
            menu.addItem(itemId++, entry.label);
        menu.addSeparator();
        menu.addItem(kViewResetLayoutItemId, "Reset Layout");
        return menu;
    }

    if (topLevelMenuIndex == 3) // Project
    {
        menu.addCommandItem(&commandManager_, kNewProjectCommand);
        menu.addCommandItem(&commandManager_, kOpenProjectBrowserCommand);
        return menu;
    }

    menu.addItem(kHelpAboutItemId, "About Creation Engine"); // Help
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
    if (topLevelMenuIndex == 2) // View
    {
        if (menuItemID == kViewResetLayoutItemId)
        {
            if (dockManager_ != nullptr) dockManager_->resetLayout();
            return;
        }
        const auto index = menuItemID - kViewPanelItemIdBase;
        if (index >= 0 && index < static_cast<int>(std::size(kDockPanelMenuEntries)) && dockManager_ != nullptr) {
            const juce::String id = kDockPanelMenuEntries[static_cast<std::size_t>(index)].id;
            // "input-bindings" isn't part of the default eager-docked
            // layout (nothing should be, per the user's own stated intent
            // -- just not yet applied to every panel) -- register it lazily
            // on first open, same shape as OpenPodEditor().
            if (id == "input-bindings") EnsureInputBindingsPanelOpen();
            else if (id == "lighting") EnsureLightPanelOpen();
            else dockManager_->activatePanel(id);
        }
        return;
    }

    if (menuItemID == kHelpAboutItemId)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Creation Engine",
                                               "Creation Engine\nPart of Creation Suite.");
    }
}

void MainComponent::openGameClient()
{
    if (!projectSession_.isValid() || activeGame_.id.isEmpty() || activeScene_.id.isEmpty()) {
        headerBar_.setStatusText("Open a game and scene before running a client.");
        return;
    }
    saveSessionToDisk(false);
    const int clientNumber = static_cast<int>(gameClients_.size()) + 1;
    const auto sceneState = ce::scene::EngineSceneSerializer::serializeScene(world_);
    gameClients_.push_back(std::make_unique<ce::runtime::GameClientWindow>(
        clientNumber, sceneState, activeGame_.name, activeScene_.name));
    headerBar_.setStatusText("Running " + juce::String(gameClients_.size()) + " game client" + (gameClients_.size() == 1 ? "" : "s"));
}

void MainComponent::possessDesignerCharacter()
{
    if (possessedEntityId_ != -1) {
        // Already possessing -- Stop is the one way out (Decision 7); a
        // second click here is a no-op rather than a re-teleport.
        headerBar_.setStatusText("Already possessing the designer character.");
        return;
    }

    // SpawnPosition(0.0f) is exactly the free-fly camera's own current
    // position (a point 0 units in front of it) -- reuses the same
    // thread-safe stateLock_-guarded snapshot HandleAssetDropped already
    // relies on, rather than reading FreeCamera's position_ directly from
    // this (message) thread.
    const auto spawnPos = viewport_.SpawnPosition(0.0f);

    entt::entity entity;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        entity = registry.create();
        auto& transform = registry.emplace<ce::engine::Transform>(entity);
        transform.position = { spawnPos.x, spawnPos.y, spawnPos.z };
    }
    const auto entityId = static_cast<std::int64_t>(entt::to_integral(entity));

    // Capsule dimensions match CharacterControllerSmoke's own values -- no
    // authored-per-character sizing yet (Phase 3's own scope note).
    if (!physicsWorld_.CreateCharacter(world_, entityId, 0.3f, 0.9f)) {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        world_.Registry().destroy(entity);
        headerBar_.setStatusText("Failed to possess designer character.");
        return;
    }

    possessedEntityId_ = entityId;
    viewport_.EnterPossessedMode();
    viewport_.SetPossessedFeetPosition(spawnPos);
    SetPlaying(true);
}

void MainComponent::componentMovedOrResized(juce::Component& component, bool, bool)
{
    if (&component == &viewport_) syncViewportRenderHost();
}

void MainComponent::componentVisibilityChanged(juce::Component& component)
{
    if (&component == &viewport_) syncViewportRenderHost();
}

void MainComponent::syncViewportRenderHost()
{
    if (viewport_.isShowing()) {
        viewportRenderHost_.setBounds(getLocalArea(&viewport_, viewport_.getLocalBounds()));
    } else {
        // Parked off-canvas, not hidden -- see viewportRenderHost_'s
        // header comment for why setVisible(false) is off the table here.
        // Same size as its last on-screen placement (irrelevant while
        // parked, but keeps the next on-screen restore's aspect ratio
        // sane for one frame if a resize happens to land while off-canvas).
        const auto size = viewportRenderHost_.getLocalBounds();
        viewportRenderHost_.setBounds(-100000, -100000, size.getWidth(), size.getHeight());
    }
}

void MainComponent::initialiseDockingWorkspace()
{
    dockManager_ = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager_);
    dockManager_->registerPanel("viewport", "Scene Viewport", std::make_unique<NonOwningPanelHost>(viewport_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel("properties", "Properties", std::make_unique<NonOwningPanelHost>(propertiesPanel_), CreationDock::DockTargetZone::Right);
    // "pods"/"pod-info" deliberately NOT registered here -- they exist only
    // while a Pod is open, via OpenPodEditor(), one pair per open Pod
    // (Editor UI/Workflow Overhaul plan, Phase 5). "input-bindings"/"lighting"
    // are the same lazy shape
    // (EnsureInputBindingsPanelOpen()/EnsureLightPanelOpen(), called from
    // the View menu instead of Content Browser) -- unlike Pods they ARE
    // listed in kDockPanelMenuEntries/the View menu (there's exactly one of
    // each, not one per opened asset), they just aren't part of the default
    // eager-docked layout the panels above are.
    dockManager_->registerPanel("materials", "Materials", std::make_unique<NonOwningPanelHost>(materialsPanel_), CreationDock::DockTargetZone::CenterTab);
    // "assets" (the old standalone Import screen) is gone -- import/export/
    // browse/place all live in Content Browser now. importPanel_ itself
    // stays alive as backing logic (importer registry, audio catalog) that
    // Content Browser and Reimport call into; it's never mounted as a panel.
    // Docked at the Bottom (not a CenterTab) so it's open by default --
    // this is meant to be glanced at constantly while building a scene,
    // not a screen you navigate to.
    dockManager_->registerPanel("content-browser", "Content Browser", std::make_unique<NonOwningPanelHost>(contentBrowserPanel_), CreationDock::DockTargetZone::Bottom);
    // "server"/"settings" not registered -- see kDockPanelMenuEntries'
    // comment above.
    dockManager_->registerPanel("log", "Log", std::make_unique<NonOwningPanelHost>(logPanel_), CreationDock::DockTargetZone::Bottom);
}

void MainComponent::OpenPodEditor(const juce::String& podName) {
    if (dockManager_ == nullptr || podName.isEmpty()) return;

    const auto editorId = "pod-editor-" + podName;
    const auto infoId = "pod-info-" + podName;

    if (openPodEditors_.find(podName) == openPodEditors_.end()) {
        OpenPodEditorEntry entry;
        entry.editor = std::make_unique<ce::views::PodEditorPanel>(frustHost_, podCatalog_, projectSession_);
        entry.info = std::make_unique<ce::views::PodInfoPanel>(podCatalog_, projectSession_, entry.editor->Graph(),
                                                                entry.editor->Registry());
        // PodEditorPanel no longer owns the Pod's identity/interface UI or
        // the node inspector itself, PodInfoPanel does.
        entry.editor->onOpenPodChanged = [info = entry.info.get()](const juce::String& name) { info->SetOpenPod(name); };
        entry.editor->onSelectedNodeChanged = [info = entry.info.get()](ce::node_system::NodeId id) { info->SetSelectedNode(id); };
        entry.editor->OpenPod(podName);

        auto* editorPanel = dockManager_->registerPanel(editorId, podName, std::make_unique<NonOwningPanelHost>(*entry.editor),
                                                         CreationDock::DockTargetZone::CenterTab);
        editorPanel->onCloseRequested = [this, podName](CreationDock::DockPanel*) { ClosePodEditor(podName); };
        auto* infoPanel = dockManager_->registerPanel(infoId, "Pod: " + podName, std::make_unique<NonOwningPanelHost>(*entry.info),
                                                       CreationDock::DockTargetZone::Right);
        infoPanel->onCloseRequested = [this, podName](CreationDock::DockPanel*) { ClosePodEditor(podName); };

        openPodEditors_[podName] = std::move(entry);
    }
    dockManager_->activatePanel(editorId);
}

void MainComponent::EnsureInputBindingsPanelOpen() {
    if (dockManager_ == nullptr) return;

    if (!dockManager_->isRegistered("input-bindings")) {
        auto* panel = dockManager_->registerPanel("input-bindings", "Input Bindings",
                                                   std::make_unique<NonOwningPanelHost>(inputBindingsPanel_),
                                                   CreationDock::DockTargetZone::Right);
        // Deferred via callAsync -- see ClosePodEditor's comment for why
        // (destroying a DockPanel synchronously from inside its own tab's
        // close handler, still on the call stack, is a confirmed crash).
        panel->onCloseRequested = [this](CreationDock::DockPanel*) {
            juce::MessageManager::callAsync([this] {
                if (dockManager_ != nullptr) dockManager_->unregisterPanel("input-bindings");
            });
        };
    }
    dockManager_->activatePanel("input-bindings");
}

void MainComponent::EnsureLightPanelOpen() {
    if (dockManager_ == nullptr) return;

    if (!dockManager_->isRegistered("lighting")) {
        auto* panel = dockManager_->registerPanel("lighting", "Lighting",
                                                   std::make_unique<NonOwningPanelHost>(lightPanel_),
                                                   CreationDock::DockTargetZone::Right);
        // Deferred via callAsync -- same reason as EnsureInputBindingsPanelOpen above.
        panel->onCloseRequested = [this](CreationDock::DockPanel*) {
            juce::MessageManager::callAsync([this] {
                if (dockManager_ != nullptr) dockManager_->unregisterPanel("lighting");
            });
        };
    }
    dockManager_->activatePanel("lighting");
}

void MainComponent::ClosePodEditor(const juce::String& podName) {
    if (dockManager_ == nullptr) return;
    // Deferred, not synchronous: this is called from DockTab::mouseDown
    // (via onCloseRequested), which is still on the call stack for the
    // very tab being closed -- destroying its DockPanel (and the
    // PodEditorPanel/PodInfoPanel it wraps) while that event handler is
    // still executing is a real, reproduced crash (confirmed via a
    // Windows crash dump: access violation reading freed memory, right at
    // entry to this function, called directly from DockTab::mouseDown
    // still on the stack). juce::MessageManager::callAsync runs the
    // actual teardown on the next message-loop iteration instead, once
    // the click has fully finished being handled -- the standard JUCE
    // fix for "don't destroy a component from inside its own callback."
    juce::MessageManager::callAsync([this, podName] {
        if (dockManager_ == nullptr) return;
        dockManager_->unregisterPanel("pod-editor-" + podName);
        dockManager_->unregisterPanel("pod-info-" + podName);
        // Unlike the old single-instance version, this instance is fully
        // destroyed (not kept around with stale content reset) -- a
        // closed Pod editor has nowhere for stale content to leak into
        // next time, since OpenPodEditor always constructs a fresh pair
        // on next open.
        openPodEditors_.erase(podName);
    });
}

void MainComponent::EnsureObjectDefinitionPanelOpen() {
    if (dockManager_ == nullptr) return;
    if (!dockManager_->isRegistered("object-definition")) {
        auto* panel = dockManager_->registerPanel("object-definition", "Object Definition",
                                                   std::make_unique<NonOwningPanelHost>(*objectDefinitionEditorPanel_),
                                                   CreationDock::DockTargetZone::Right);
        panel->onCloseRequested = [this](CreationDock::DockPanel*) { CloseObjectDefinitionPanel(); };
    }
    dockManager_->activatePanel("object-definition");
}

void MainComponent::CloseObjectDefinitionPanel() {
    if (dockManager_ == nullptr) return;
    dockManager_->unregisterPanel("object-definition");
}

void MainComponent::SetPlaying(bool playing) {
    if (isPlaying_ == playing) {
        return;
    }

    isPlaying_ = playing;
    viewport_.SetPlaying(playing); // hides SceneFlags::editorOnly entities (e.g. the cart) while playing.
    const auto tick = static_cast<std::int64_t>(world_.CurrentTick());
    if (playing) {
        frustHost_.beginPlay(tick);
    } else {
        frustHost_.endPlay(tick);
    }
    headerBar_.setPlaybackVisualState(isPlaying_, false);
    headerBar_.setStatusText(isPlaying_ ? "Playing" : "Editing");
}

void MainComponent::timerCallback() {
    // Catch-all for viewportRenderHost_ sync (see its own comment):
    // viewport_'s bounds/visible flag can already be in their final state
    // before the top-level window itself actually becomes visible, so
    // neither the ComponentListener callbacks nor MainComponent::resized()
    // are guaranteed to fire again at the moment isShowing() actually
    // flips true. Cheap at 30 Hz -- just an isShowing() check plus,
    // ordinarily, a no-op setBounds once already in sync.
    syncViewportRenderHost();

    // Editor UI/Workflow Overhaul plan, Phase 3: Properties is now driven
    // directly by whatever ViewportComponent::desktopPick selects (via
    // EditorInteraction), independent of Hierarchy (removed) ever existing.
    if (const auto selection = interactions_.takeSelectionChange())
        propertiesPanel_.SetSelectedEntity(*selection);
    if (pendingSceneTransitionId_.isNotEmpty()) {
        const auto requestedScene = pendingSceneTransitionId_;
        pendingSceneTransitionId_.clear();
        const bool resumeAfterTransition = isPlaying_;
        if (resumeAfterTransition) SetPlaying(false);
        selectScene(requestedScene);
        if (resumeAfterTransition) SetPlaying(true);
    }
    if (isPlaying_) {
        RunPreUpdatePhase();
        const float physicsElapsedSeconds = RunPhysicsResolvePhase();
        RunPostPhysicsPhase(physicsElapsedSeconds);
    }
    // Engine Loop Decoupling plan, Phase 2: publish a fresh FrameSnapshot
    // every tick, unconditionally -- not just while isPlaying_. The
    // viewport renders continuously either way (orbiting the camera while
    // stopped, previewing an inspector edit), so it needs a snapshot
    // refreshed on the same cadence regardless of Play state. This is the
    // one place per tick that resolves MeshAssetReferences and advances
    // every Animator now (moved out of ViewportComponent::renderOpenGL()
    // -- see its own comment).
    viewport_.PublishFrameSnapshot();
    propertiesPanel_.Refresh();
}

void MainComponent::RunPreUpdatePhase() {
    // ce::engine::EngineTickPhase::PreUpdate (engine/tick_phase.h).
    //
    // Input Binding System plan: poll raw keyboard/mouse/controller state
    // once, before anything below reads it -- a Pod's on_tick this same
    // tick sees this tick's poll (core.input.isActionActive et al.), not a
    // stale one from last tick.
    inputActionSystem_.PollOncePerFrame();
    // GS6: runs every attached ScriptComponent's on_tick (and on_start, on
    // an entity's first playing tick) before advancing World's tick
    // counter -- the same Simulation::Step CreationEngineServer's main
    // loop calls, so the editor and server genuinely execute scripts
    // identically. 1/30s matches this timer's own 30 Hz rate
    // (startTimerHz(30) below).
    ce::engine::Simulation::Step(world_, 1.0f / 30.0f);
    ce::engine::FoundationGameplay::Step(world_, {}, 1.0f / 30.0f);
}

float MainComponent::RunPhysicsResolvePhase() {
    // ce::engine::EngineTickPhase::PhysicsResolve -- the boundary between
    // PreUpdate's declared intent and PostPhysics's settled results, not a
    // phase gameplay code runs in.
    //
    // Jolt runs on its own fixed 60 Hz accumulator, decoupled from this
    // 30 Hz UI timer (Core Architectural Invariant 2, Jolt vendoring
    // plan) -- real measured elapsed time, not this timer's own assumed
    // 1/30s literal (which PreUpdate's calls still use, a separate,
    // pre-existing gap this doesn't fix).
    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const float physicsElapsedSeconds = lastPhysicsAdvanceSeconds_ > 0.0
        ? static_cast<float>(nowSeconds - lastPhysicsAdvanceSeconds_) : 0.0f;
    lastPhysicsAdvanceSeconds_ = nowSeconds;
    const float physicsAlpha = physicsWorld_.Advance(world_, physicsElapsedSeconds);
    {
        // InterpolateTransforms() itself doesn't lock (its usual caller,
        // the render pass, already holds this same lock for its whole
        // draw pass -- see ViewportComponent.cpp) -- this call site needs
        // its own, since nothing above holds it once Advance() returns.
        std::lock_guard<std::mutex> registryLock(world_.RegistryMutex());
        physicsWorld_.InterpolateTransforms(world_, physicsAlpha);
    }
    return physicsElapsedSeconds;
}

void MainComponent::RunPostPhysicsPhase(float physicsElapsedSeconds) {
    // ce::engine::EngineTickPhase::PostPhysics -- safe to read this same
    // tick's fresh physics results (a collision that just happened, a
    // character's just-resolved position).
    if (possessedEntityId_ != -1) {
        // JPH::CharacterVirtual is its own separate, manually-driven
        // system (not tracked by PhysicsResolve's Advance() -- see
        // PhysicsWorld.h's own comment on CreateCharacter), so it needs
        // its own per-tick update here, before frustHost_.tick() below
        // drains this tick's collision events.
        const auto cameraForward = viewport_.CameraForward();
        const float forwardYawRadians = std::atan2(cameraForward.x, -cameraForward.z);
        possessedCharacter_.Update(world_, physicsWorld_, inputActionSystem_,
                                  possessedEntityId_, forwardYawRadians, physicsElapsedSeconds);

        juce::Vector3D<float> feetPosition;
        {
            std::lock_guard<std::mutex> registryLock(world_.RegistryMutex());
            const auto entityHandle = static_cast<entt::entity>(possessedEntityId_);
            if (world_.Registry().valid(entityHandle)) {
                const auto& transform = world_.Registry().get<ce::engine::Transform>(entityHandle);
                feetPosition = { transform.position.x, transform.position.y, transform.position.z };
            }
        }
        viewport_.SetPossessedFeetPosition(feetPosition);
    }
    frustHost_.tick(static_cast<std::int64_t>(world_.CurrentTick()));
}

void MainComponent::createNewProject()
{
    auto* prompt = new juce::AlertWindow("Create New Engine Project",
                                         "Enter a name for your new Creation Engine project container:",
                                         juce::MessageBoxIconType::QuestionIcon);
    prompt->addTextEditor("projectName", "");
    prompt->addButton("Create Project", 1);
    prompt->addButton("Cancel", 0);

    auto options = juce::Component::SafePointer<MainComponent>(this);
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([options, prompt](int result) mutable
    {
        std::unique_ptr<juce::AlertWindow> dialog(prompt);
        if (result != 1 || options == nullptr)
            return;

        auto name = dialog->getTextEditorContents("projectName").trim();
        if (name.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", "Project name cannot be empty.");
            return;
        }

        juce::String err;
        if (! creation::assets::ProjectWorkspaceService::createProject(options->suiteSettings_, creation::assets::SuiteAppDomain::engine, name, "1.0.0", "1.0.0", options->projectSession_, err))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", err);
            return;
        }

        options->headerBar_.setProjectLabel("Project: " + options->projectSession_.getManifest().projectName);
        options->loadPodsForActiveProject();
        juce::String activateError;
        if (!options->openActiveGame(activateError))
            options->headerBar_.setStatusText("Created project, but could not open its game: " + activateError);
        options->saveAppSettings();
        options->headerBar_.setStatusText("Created project: " + options->projectSession_.getManifest().projectName);
    }), true);
}

void MainComponent::openProject(const juce::String& projectId)
{
    juce::String err;
    if (! creation::assets::ProjectWorkspaceService::openProject(suiteSettings_, projectId, projectSession_, err))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", err);
        return;
    }

    headerBar_.setProjectLabel("Project: " + projectSession_.getManifest().projectName);
    loadPodsForActiveProject();
    if (!openActiveGame(err)) {
        headerBar_.setStatusText("Project opened, but its game could not load: " + err);
        return;
    }
    saveAppSettings();
}

void MainComponent::saveSessionToDisk(bool userInitiated)
{
    if (! projectSession_.isValid())
    {
        if (userInitiated)
            headerBar_.setStatusText("No active project session to save.");
        return;
    }

    if (activeGame_.id.isEmpty() || activeScene_.id.isEmpty()) {
        if (userInitiated) headerBar_.setStatusText("No active game scene to save.");
        return;
    }
    juce::String sceneError;
    if (!ce::project::EngineGameDocumentStore::saveScene(projectSession_, activeGame_, activeScene_, world_, sceneError)) {
        headerBar_.setStatusText("Scene save failed: " + sceneError);
        return;
    }

    juce::String commitError;
    if (! projectSession_.commit(commitError))
    {
        headerBar_.setStatusText("Project save failed: " + commitError);
        return;
    }

    if (userInitiated)
        headerBar_.setStatusText("Saved " + activeGame_.name + " / " + activeScene_.name);
}

void MainComponent::loadSessionFromDisk()
{
    juce::String error;
    if (!openActiveGame(error) && error.isNotEmpty()) headerBar_.setStatusText("Scene load failed: " + error);
}

bool MainComponent::openActiveGame(juce::String& errorMessage)
{
    if (!projectSession_.isValid()) {
        errorMessage = "No Suite project is open.";
        return false;
    }
    juce::Array<ce::project::GameDocumentInfo> games;
    if (!ce::project::EngineGameDocumentStore::ensureInitialGame(projectSession_, games, errorMessage)) return false;
    if (games.isEmpty()) {
        errorMessage = "The project does not contain an Engine game.";
        return false;
    }
    juce::String settingsError;
    const auto settings = creation::services::SuiteVfsJsonStore::loadJson("engine-settings.json", settingsError);
    const auto* settingsObject = settings.getDynamicObject();
    const auto lastGameId = settingsObject != nullptr ? settingsObject->getProperty("lastOpenedGameId").toString() : juce::String{};
    const auto lastSceneId = settingsObject != nullptr ? settingsObject->getProperty("lastOpenedSceneId").toString() : juce::String{};
    activeGame_ = games.getFirst();
    for (const auto& game : games)
        if (game.id == lastGameId) { activeGame_ = game; break; }
    for (const auto& scene : activeGame_.scenes)
        if (scene.id == lastSceneId) { activeScene_ = scene; break; }
    if (activeScene_.id.isEmpty())
        for (const auto& scene : activeGame_.scenes)
            if (scene.id == activeGame_.entrySceneId) { activeScene_ = scene; break; }
    if (activeScene_.id.isEmpty() && !activeGame_.scenes.isEmpty()) activeScene_ = activeGame_.scenes.getFirst();
    juce::String inputBindingsError;
    inputActionSystem_.LoadForGame(projectSession_, activeGame_, inputBindingsError);
    RefreshComboEventNodes();
    inputBindingsPanel_.SetActiveGame(activeGame_);
    importPanel_.SetProjectContent(&projectSession_, activeGame_.assetRoot());
    djehutiImportWatcher_.SetProjectContent(&projectSession_, projectSession_.getProjectId());
    contentBrowserPanel_.SetProjectContent(&projectSession_);
    if (!ce::project::EngineGameDocumentStore::loadScene(projectSession_, activeGame_, activeScene_, world_, errorMessage)) return false;
    viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
    frustHost_.prepareLevel(static_cast<std::int64_t>(world_.CurrentTick()));
    propertiesPanel_.Refresh();
    games_ = games;
    headerBar_.setProjectLabel("Project: " + projectSession_.getManifest().projectName + " | " + activeGame_.name + " / " + activeScene_.name);
    saveAppSettings();
    return true;
}

void MainComponent::RefreshComboEventNodes()
{
    std::vector<std::string> comboNames;
    for (const auto& combo : inputActionSystem_.Bindings().combos) comboNames.push_back(combo.name.toStdString());
    std::string error;
    if (!frustHost_.RefreshComboEventNodes(comboNames, error))
        headerBar_.setStatusText("Could not register input combo events: " + juce::String(error));
}

void MainComponent::HandleAssetDropped(const juce::String& description, juce::Point<int> localPosition)
{
    const auto parts = juce::StringArray::fromTokens(description, "|", "");
    if (parts.size() < 5 || parts[0] != "asset") return;
    const auto kind = creation::assets::assetKindFromStorageToken(parts[1]);
    const auto id = parts[2];
    const auto versionId = parts[3];
    const auto displayName = parts[4];

    // Ray-cast the drop point against the ground plane (y=0, same
    // convention FoundationGameplay's own ground uses) so an asset lands
    // roughly where it was dropped rather than always at a fixed spot in
    // front of the camera; SpawnPosition() is only the fallback for a ray
    // that can't hit that plane (e.g. dropped above the horizon, looking
    // up).
    auto dropPosition = ce::scene::ToVec3(viewport_.SpawnPosition());
    juce::Vector3D<float> rayOrigin, rayDirection;
    if (viewport_.desktopRay(localPosition.toFloat(), rayOrigin, rayDirection) && rayDirection.y < -0.001f) {
        const float t = -rayOrigin.y / rayDirection.y;
        dropPosition = ce::scene::ToVec3(juce::Vector3D<float>{ rayOrigin.x + rayDirection.x * t, 0.0f,
                                                                 rayOrigin.z + rayDirection.z * t });
    }

    // Placing an asset always goes through an Object Definition -- never a
    // bare entity referencing a raw mesh directly. Per
    // docs/ENGINE_ASSET_MANAGEMENT_PLAN.md: "Placing an object creates a
    // scene entity referencing the object asset." A dropped raw mesh gets
    // (or reuses) a single-component wrapper definition so it has a real,
    // reopenable object identity, same as any hand-built Object Definition.
    juce::String definitionId;
    if (kind == creation::assets::AssetKind::render) {
        definitionId = ce::scene::FindWrapperDefinitionForRenderAsset(objectDefinitions_, id);
        if (definitionId.isEmpty()) {
            ce::scene::ObjectDefinition wrapper;
            wrapper.id = ce::scene::GenerateWrapperDefinitionName(objectDefinitions_, displayName);
            wrapper.displayName = displayName;
            ce::scene::ObjectComponentEntry meshComponent;
            meshComponent.kind = ce::scene::ObjectComponentKind::Mesh;
            meshComponent.meshAssetId = id;
            meshComponent.meshAssetVersionId = versionId;
            wrapper.components.push_back(std::move(meshComponent));

            juce::String upsertError;
            if (!objectDefinitions_.upsert(wrapper, upsertError)) {
                headerBar_.setStatusText("Could not place \"" + displayName + "\": " + upsertError);
                return;
            }
            juce::String saveError;
            if (!objectDefinitions_.Save(projectSession_, wrapper.id, saveError)) {
                headerBar_.setStatusText("Could not place \"" + displayName + "\": " + saveError);
                return;
            }
            definitionId = wrapper.id;
        }
    } else if (kind == creation::assets::AssetKind::objectDefinition) {
        definitionId = displayName;
    } else {
        return;
    }

    juce::String error;
    // instantiate() takes the registry lock itself -- don't take it again here.
    const auto result = ce::scene::ObjectFactory::instantiate(world_, objectDefinitions_, definitionId, dropPosition, error);
    if (result.root == entt::null) {
        headerBar_.setStatusText("Could not place \"" + displayName + "\": " + error);
        return;
    }

    // Materializes the mesh into the runtime GPU cache right away (rather
    // than leaving it to resolve lazily next frame) so a skinned model's
    // Skeleton/Animator can be attached too -- ObjectFactory::instantiate
    // only emplaces MeshAssetReference, not Skeleton/Animator, so that part
    // still happens here. Every Mesh component is now its own child entity
    // (never result.root itself, per ObjectFactory::instantiateDefinition's
    // uniform-child-per-mesh rule) -- find the child actually carrying this
    // asset's MeshAssetReference rather than assuming the root is it.
    viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
    if (kind == creation::assets::AssetKind::render) {
        const auto asset = viewport_.Catalog().Find(id);
        if (asset.mesh != nullptr) {
            std::lock_guard<std::mutex> lock(world_.RegistryMutex());
            auto& registry = world_.Registry();
            entt::entity meshEntity = entt::null;
            for (const auto candidate : result.entities) {
                if (const auto* ref = registry.try_get<ce::scene::MeshAssetReference>(candidate); ref != nullptr && ref->assetId == id) {
                    meshEntity = candidate;
                    break;
                }
            }
            if (meshEntity != entt::null) {
                if (asset.skeleton != nullptr) registry.emplace<ce::scene::Skeleton>(meshEntity, *asset.skeleton);
                if (asset.animationClips != nullptr && !asset.animationClips->empty())
                    registry.emplace<ce::scene::Animator>(meshEntity, ce::scene::Animator{ asset.animationClips, 0, 0.0f, false, true });
            }
        } else {
            headerBar_.setStatusText("Placed \"" + displayName + "\", but its mesh hasn't loaded yet.");
        }
    }
}

void MainComponent::selectGame(const juce::String& gameId)
{
    for (const auto& game : games_) {
        if (game.id != gameId) continue;
        saveSessionToDisk(false);
        activeGame_ = game;
        activeScene_ = {};
        for (const auto& scene : activeGame_.scenes)
            if (scene.id == activeGame_.entrySceneId) { activeScene_ = scene; break; }
        if (activeScene_.id.isEmpty() && !activeGame_.scenes.isEmpty()) activeScene_ = activeGame_.scenes.getFirst();
        juce::String inputBindingsError;
        inputActionSystem_.LoadForGame(projectSession_, activeGame_, inputBindingsError);
        RefreshComboEventNodes();
        inputBindingsPanel_.SetActiveGame(activeGame_);
        importPanel_.SetProjectContent(&projectSession_, activeGame_.assetRoot());
        djehutiImportWatcher_.SetProjectContent(&projectSession_, projectSession_.getProjectId());
        contentBrowserPanel_.SetProjectContent(&projectSession_);
        juce::String error;
        if (!ce::project::EngineGameDocumentStore::loadScene(projectSession_, activeGame_, activeScene_, world_, error))
            headerBar_.setStatusText("Could not open game: " + error);
        else {
            viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
            frustHost_.prepareLevel(static_cast<std::int64_t>(world_.CurrentTick()));
            saveAppSettings();
        }
        return;
    }
}

void MainComponent::selectScene(const juce::String& sceneId)
{
    for (const auto& scene : activeGame_.scenes) {
        if (scene.id != sceneId) continue;
        saveSessionToDisk(false);
        juce::String error;
        if (!ce::project::EngineGameDocumentStore::loadScene(projectSession_, activeGame_, scene, world_, error))
            headerBar_.setStatusText("Could not open scene: " + error);
        else {
            activeScene_ = scene;
            viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
            frustHost_.prepareLevel(static_cast<std::int64_t>(world_.CurrentTick()));
            saveAppSettings();
        }
        return;
    }
}

void MainComponent::createGame()
{
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    ce::views::showNewGameDialog([safeThis](bool created, juce::String name, ce::project::StarterGameTemplate chosenTemplate) {
        if (!created || safeThis == nullptr) return;

        ce::project::GameDocumentInfo game;
        ce::project::SceneDocumentInfo scene;
        juce::String error;
        const auto templateSceneId = chosenTemplate.starterSceneTemplateId.isEmpty() ? "DefaultScene" : chosenTemplate.starterSceneTemplateId;
        if (!ce::project::EngineGameDocumentStore::createGame(safeThis->projectSession_, name, game, scene, error, templateSceneId) ||
            !safeThis->projectSession_.commit(error)) {
            safeThis->headerBar_.setStatusText("Could not create game: " + error);
            return;
        }
        safeThis->games_.add(game);
        safeThis->activeGame_ = game;
        safeThis->activeScene_ = scene;
        safeThis->importPanel_.SetProjectContent(&safeThis->projectSession_, safeThis->activeGame_.assetRoot());
        safeThis->djehutiImportWatcher_.SetProjectContent(&safeThis->projectSession_, safeThis->projectSession_.getProjectId());
        safeThis->contentBrowserPanel_.SetProjectContent(&safeThis->projectSession_);

        // createGame() only writes the new scene's document -- it doesn't
        // load it into the live world_, the same gap selectGame/selectScene
        // already fix for switching TO an existing game. Load it here too,
        // required before any starter-content placement below (which needs
        // a live world_ to instantiate into).
        if (!ce::project::EngineGameDocumentStore::loadScene(safeThis->projectSession_, safeThis->activeGame_,
                                                              safeThis->activeScene_, safeThis->world_, error)) {
            safeThis->headerBar_.setStatusText("Created " + game.name + ", but could not load its scene: " + error);
            return;
        }
        safeThis->viewport_.ResolveProjectAssets(safeThis->projectSession_, safeThis->suiteSettings_);

        safeThis->PlaceStarterContent(chosenTemplate);

        safeThis->frustHost_.prepareLevel(static_cast<std::int64_t>(safeThis->world_.CurrentTick()));
        safeThis->saveAppSettings();
        safeThis->headerBar_.setStatusText("Created " + game.name + " / " + scene.name);
    });
}

void MainComponent::PlaceStarterContent(const ce::project::StarterGameTemplate& chosenTemplate)
{
    if (chosenTemplate.starterModelAssetId.isEmpty()) return;

    juce::String error;
    juce::File packDirectory;
    if (!ce::assets::AssetPackStore::materializePack(ce::assets::EngineAssetPack::packId, ce::assets::EngineAssetPack::version,
                                                     packDirectory, error)) {
        headerBar_.setStatusText("Scene created, but starter content could not be loaded: " + error);
        return;
    }
    ce::assets::AssetPackStore::Manifest manifest;
    if (!ce::assets::AssetPackStore::readManifest(ce::assets::EngineAssetPack::packId, ce::assets::EngineAssetPack::version,
                                                  manifest, error)) {
        headerBar_.setStatusText("Scene created, but starter content could not be loaded: " + error);
        return;
    }
    const auto* declared = std::find_if(manifest.assets.begin(), manifest.assets.end(), [&](const auto& asset) {
        return asset.id == chosenTemplate.starterModelAssetId;
    });
    if (declared == manifest.assets.end() || declared->payload.isEmpty()) {
        headerBar_.setStatusText("Scene created, but its starter model \"" + chosenTemplate.starterModelAssetId + "\" was not found in the Engine Pack.");
        return;
    }
    const auto sourceFile = packDirectory.getChildFile(declared->payload);

    ce::import::ImportContext context;
    context.world = &world_;
    context.catalog = &viewport_.Catalog();
    context.viewport = &viewport_;
    context.projectSession = &projectSession_;
    context.gameAssetRoot = activeGame_.assetRoot();
    context.objectDefinitions = &objectDefinitions_;

    ce::import::GltfAssetImporter importer;
    const auto result = importer.Import(sourceFile, context);
    if (!result.success || result.createdAssetId.isEmpty()) {
        headerBar_.setStatusText("Scene created, but starter content import failed: " + result.message);
        return;
    }

    auto definitionId = ce::scene::FindWrapperDefinitionForRenderAsset(objectDefinitions_, result.createdAssetId);
    if (definitionId.isEmpty()) {
        ce::scene::ObjectDefinition wrapper;
        wrapper.id = ce::scene::GenerateWrapperDefinitionName(objectDefinitions_, chosenTemplate.displayName);
        wrapper.displayName = chosenTemplate.displayName;
        ce::scene::ObjectComponentEntry meshComponent;
        meshComponent.kind = ce::scene::ObjectComponentKind::Mesh;
        meshComponent.meshAssetId = result.createdAssetId;
        wrapper.components.push_back(std::move(meshComponent));

        juce::String upsertError;
        if (!objectDefinitions_.upsert(wrapper, upsertError)) {
            headerBar_.setStatusText("Scene created, but starter content could not be placed: " + upsertError);
            return;
        }
        juce::String saveError;
        if (!objectDefinitions_.Save(projectSession_, wrapper.id, saveError)) {
            headerBar_.setStatusText("Scene created, but starter content could not be placed: " + saveError);
            return;
        }
        definitionId = wrapper.id;
    }

    // Placed at the scene's own origin -- the starter environments are
    // self-contained layouts authored around (0,0,0).
    const auto instantiation = ce::scene::ObjectFactory::instantiate(world_, objectDefinitions_, definitionId, ce::engine::Vec3{}, error);
    if (instantiation.root == entt::null) {
        headerBar_.setStatusText("Scene created, but starter content could not be placed: " + error);
        return;
    }
    viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);

    // Persist the placement immediately -- a crash right after creation
    // shouldn't lose it.
    juce::String saveSceneError;
    if (ce::project::EngineGameDocumentStore::saveScene(projectSession_, activeGame_, activeScene_, world_, saveSceneError))
        projectSession_.commit(saveSceneError);
}

void MainComponent::createScene()
{
    if (activeGame_.id.isEmpty()) return;
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    ce::views::showNewGameDialog([safeThis](bool created, juce::String name, ce::project::StarterGameTemplate chosenTemplate) {
        if (!created || safeThis == nullptr) return;

        ce::project::SceneDocumentInfo scene;
        juce::String error;
        const auto templateSceneId = chosenTemplate.starterSceneTemplateId.isEmpty() ? "DefaultScene" : chosenTemplate.starterSceneTemplateId;
        if (!ce::project::EngineGameDocumentStore::createScene(safeThis->projectSession_, safeThis->activeGame_, name, scene,
                                                                error, templateSceneId) ||
            !safeThis->projectSession_.commit(error)) {
            safeThis->headerBar_.setStatusText("Could not create scene: " + error);
            return;
        }
        for (auto& game : safeThis->games_)
            if (game.id == safeThis->activeGame_.id) game = safeThis->activeGame_;

        if (!ce::project::EngineGameDocumentStore::loadScene(safeThis->projectSession_, safeThis->activeGame_, scene,
                                                              safeThis->world_, error)) {
            safeThis->headerBar_.setStatusText("Created scene, but could not load it: " + error);
            return;
        }
        safeThis->activeScene_ = scene;
        safeThis->viewport_.ResolveProjectAssets(safeThis->projectSession_, safeThis->suiteSettings_);

        safeThis->PlaceStarterContent(chosenTemplate);

        safeThis->frustHost_.prepareLevel(static_cast<std::int64_t>(safeThis->world_.CurrentTick()));
        safeThis->saveAppSettings();
        safeThis->headerBar_.setStatusText("Created scene: " + scene.name);
    });
}

void MainComponent::RenameGame(const juce::String& gameId)
{
    const auto* found = std::find_if(games_.begin(), games_.end(), [&](const auto& g) { return g.id == gameId; });
    if (found == games_.end()) return;

    auto* dialog = new juce::AlertWindow("Rename Game", "New name for \"" + found->name + "\":", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", found->name);
    dialog->addButton("Rename", 1);
    dialog->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, dialog, gameId](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(dialog);
        if (result != 1 || safeThis == nullptr) return;
        juce::String error;
        if (!ce::project::EngineGameDocumentStore::renameGame(safeThis->projectSession_, safeThis->games_, gameId,
                                                               owned->getTextEditorContents("name"), error)) {
            safeThis->headerBar_.setStatusText("Could not rename game: " + error);
            return;
        }
        if (safeThis->activeGame_.id == gameId)
            for (const auto& game : safeThis->games_)
                if (game.id == gameId) safeThis->activeGame_ = game;
        safeThis->contentBrowserPanel_.Refresh();
    }), true);
}

void MainComponent::RenameScene(const juce::String& gameId, const juce::String& sceneId)
{
    auto* game = std::find_if(games_.begin(), games_.end(), [&](const auto& g) { return g.id == gameId; });
    if (game == games_.end()) return;
    const auto* scene = std::find_if(game->scenes.begin(), game->scenes.end(), [&](const auto& s) { return s.id == sceneId; });
    if (scene == game->scenes.end()) return;

    auto* dialog = new juce::AlertWindow("Rename Scene", "New name for \"" + scene->name + "\":", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", scene->name);
    dialog->addButton("Rename", 1);
    dialog->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, dialog, gameId, sceneId](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(dialog);
        if (result != 1 || safeThis == nullptr) return;
        auto* targetGame = std::find_if(safeThis->games_.begin(), safeThis->games_.end(),
                                        [&](const auto& g) { return g.id == gameId; });
        if (targetGame == safeThis->games_.end()) return;

        juce::String error;
        if (!ce::project::EngineGameDocumentStore::renameScene(safeThis->projectSession_, *targetGame, sceneId,
                                                                owned->getTextEditorContents("name"), error)) {
            safeThis->headerBar_.setStatusText("Could not rename scene: " + error);
            return;
        }
        if (safeThis->activeGame_.id == gameId) safeThis->activeGame_ = *targetGame;
        if (safeThis->activeScene_.id == sceneId)
            for (const auto& s : targetGame->scenes)
                if (s.id == sceneId) safeThis->activeScene_ = s;
        safeThis->contentBrowserPanel_.Refresh();
    }), true);
}

bool MainComponent::ensureProjectSessionActive(juce::String& errorMessage)
{
    if (projectSession_.isValid())
        return true;

    juce::String settingsError;
    auto settings = creation::services::SuiteVfsJsonStore::loadJson("engine-settings.json", settingsError);
    if (auto* settingsObject = settings.getDynamicObject())
    {
        auto lastProjectId = settingsObject->getProperty("lastOpenedProjectId").toString();
        if (lastProjectId.isNotEmpty())
        {
            if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings_, lastProjectId, projectSession_, errorMessage))
            {
                headerBar_.setProjectLabel("Project: " + projectSession_.getManifest().projectName);
                loadPodsForActiveProject();
                if (!openActiveGame(errorMessage)) return false;
                return true;
            }
        }
    }

    auto availableProjects = creation::assets::ProjectContainerService::listProjects(
        suiteSettings_, errorMessage);

    if (! availableProjects.isEmpty())
    {
        if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings_, availableProjects.getFirst().projectId, projectSession_, errorMessage))
        {
            headerBar_.setProjectLabel("Project: " + projectSession_.getManifest().projectName);
            loadPodsForActiveProject();
            if (!openActiveGame(errorMessage)) return false;
            return true;
        }
    }

    createNewProject();
    return false;
}

void MainComponent::loadPodsForActiveProject()
{
    juce::String error;
    if (!podCatalog_.LoadAll(projectSession_, error))
        headerBar_.setStatusText("Some Pods could not be loaded: " + error);

    juce::String objectDefinitionError;
    if (!objectDefinitions_.LoadAll(projectSession_, objectDefinitionError))
        headerBar_.setStatusText("Some Object Definitions could not be loaded: " + objectDefinitionError);
}

void MainComponent::saveAppSettings()
{
    auto* object = new juce::DynamicObject();
    if (projectSession_.isValid())
        object->setProperty("lastOpenedProjectId", projectSession_.getProjectId());
    if (activeGame_.id.isNotEmpty())
        object->setProperty("lastOpenedGameId", activeGame_.id);
    if (activeScene_.id.isNotEmpty())
        object->setProperty("lastOpenedSceneId", activeScene_.id);

    juce::String errorMessage;
    creation::services::SuiteVfsJsonStore::saveJson("engine-settings.json", juce::var(object), errorMessage);
}

void MainComponent::loadAppSettings()
{
    // The actual restore (last-opened project) happens inline in ensureProjectSessionActive(),
    // which is where it's actually needed (before deciding whether to create a new project) --
    // this function exists so save/load read as a matched pair, same shape as CreationStation's.
}
