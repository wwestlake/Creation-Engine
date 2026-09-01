#include "MainComponent.h"

#include <creation/services/SuiteVfsJsonStore.h>

#include <creation/ui/CreationSuiteLogos.h>
#include "Scene/EngineSceneSerializer.h"
#include "Project/EngineGameDocument.h"
#include "engine/foundation_gameplay.h"

namespace {
constexpr juce::CommandID kRunGameClientCommand = 0x1001;
constexpr juce::CommandID kUndoInteractionCommand = 0x1002;
constexpr juce::CommandID kRedoInteractionCommand = 0x1003;

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
    : viewport_(world_, interactions_),
      hierarchyPanel_(world_, viewport_),
      transformPanel_(world_),
      pbrMaterialPanel_(world_),
      importPanel_(world_, viewport_, projectSession_),
      lightPanel_(viewport_) {
    commandManager_.registerAllCommandsForTarget(this);
    commandManager_.getKeyMappings()->addKeyPress(
        kRunGameClientCommand,
        juce::KeyPress('G', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0));
    commandManager_.getKeyMappings()->addKeyPress(kUndoInteractionCommand, juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    commandManager_.getKeyMappings()->addKeyPress(kRedoInteractionCommand, juce::KeyPress('Y', juce::ModifierKeys::ctrlModifier, 0));
    addKeyListener(commandManager_.getKeyMappings());

    juce::String suiteErr;
    suiteSettings_ = suiteSettingsStore_.load(suiteErr);

    std::string frustError;
    if (!frustHost_.loadBundled(frustError)) {
        juce::Logger::writeToLog("Creation Engine FRust host: " + juce::String(frustError));
    }
    frustAutomationPanel_ = std::make_unique<ce::views::FrustLogicPanel>(frustHost_.nodeLibraries());
    frustHost_.setSceneTransitionRequestHandler([this](const std::string& reference) {
        const juce::String sceneReference(reference);
        for (const auto& scene : activeGame_.scenes)
            if (scene.id == sceneReference || scene.name == sceneReference) { pendingSceneTransitionId_ = scene.id; return; }
    });
    frustHost_.setActiveGameIdProvider([this] { return activeGame_.id.toStdString(); });
    frustHost_.setActiveSceneIdProvider([this] { return activeScene_.id.toStdString(); });

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
        headerBar_.setStatusText("Stopped");
    };
    headerBar_.setStatusText("Editing");

    addAndMakeVisible(viewModeBar_);
    viewModeBar_.onModeSelected = [this](ce::WorkspaceMode mode) { SetActiveMode(mode); };
    runGameButton_.onClick = [this] { openGameClient(); };
    runGameButton_.setTooltip("Open an isolated game client window. Click again for another local multiplayer client.");
    addAndMakeVisible(runGameButton_);

    hierarchyPanel_.onSelectionChanged = [this](entt::entity entity) {
        interactions_.select(entity);
        transformPanel_.SetSelectedEntity(entity);
        pbrMaterialPanel_.SetSelectedEntity(entity);
    };
    gameScenePanel_.onGameSelected = [this](const juce::String& gameId) { selectGame(gameId); };
    gameScenePanel_.onSceneSelected = [this](const juce::String& sceneId) { selectScene(sceneId); };
    gameScenePanel_.onCreateGameRequested = [this] { createGame(); };
    gameScenePanel_.onCreateSceneRequested = [this] { createScene(); };
    gameScenePanel_.onSaveRequested = [this] { saveSessionToDisk(true); };
    hierarchyPanel_.onEntityDestroying = [this](entt::entity entity) {
        frustHost_.notifyObjectDestroyed(entity, static_cast<std::int64_t>(world_.CurrentTick()));
    };
    inspectorTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    inspectorTitle_.setColour(juce::Label::textColourId, juce::Colours::white);
    tickLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    initialiseDockingWorkspace();

    SetActiveMode(ce::WorkspaceMode::Scene);
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
    viewModeBar_.setBounds(bounds.removeFromTop(56));
    runGameButton_.setBounds(getWidth() - 170, 105, 158, 34);

    if (dockManager_ != nullptr) dockManager_->setBounds(bounds);
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.add(kRunGameClientCommand);
    commands.add(kUndoInteractionCommand);
    commands.add(kRedoInteractionCommand);
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
    return false;
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

void MainComponent::SetActiveMode(ce::WorkspaceMode mode) {
    activeMode_ = mode;

    viewModeBar_.SetActiveMode(mode);
    if (dockManager_ == nullptr) return;
    static constexpr const char* panelIds[] = { "viewport", "logic", "materials", "assets", "server", "settings" };
    dockManager_->activatePanel(panelIds[static_cast<std::size_t>(mode)]);
}

void MainComponent::initialiseDockingWorkspace()
{
    dockManager_ = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager_);
    dockManager_->registerPanel("hierarchy", "Hierarchy", std::make_unique<NonOwningPanelHost>(hierarchyPanel_), CreationDock::DockTargetZone::Left);
    dockManager_->registerPanel("game-scene", "Game & Scene", std::make_unique<NonOwningPanelHost>(gameScenePanel_), CreationDock::DockTargetZone::Left);
    dockManager_->registerPanel("viewport", "Scene Viewport", std::make_unique<NonOwningPanelHost>(viewport_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel("transform", "Transform", std::make_unique<NonOwningPanelHost>(transformPanel_), CreationDock::DockTargetZone::Right);
    dockManager_->registerPanel("materials-pbr", "Material Inspector", std::make_unique<NonOwningPanelHost>(pbrMaterialPanel_), CreationDock::DockTargetZone::Right);
    dockManager_->registerPanel("lighting", "Lighting", std::make_unique<NonOwningPanelHost>(lightPanel_), CreationDock::DockTargetZone::Right);
    dockManager_->registerPanel("logic", "FRust Logic", std::make_unique<NonOwningPanelHost>(*frustAutomationPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel("materials", "Materials", std::make_unique<NonOwningPanelHost>(materialsPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel("assets", "Assets & Import", std::make_unique<NonOwningPanelHost>(importPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel("server", "Server", std::make_unique<NonOwningPanelHost>(serverPanel_), CreationDock::DockTargetZone::Bottom);
    dockManager_->registerPanel("settings", "Settings", std::make_unique<NonOwningPanelHost>(settingsPanel_), CreationDock::DockTargetZone::Right);
    dockManager_->registerPanel("runtime-status", "Runtime Status", std::make_unique<NonOwningPanelHost>(tickLabel_), CreationDock::DockTargetZone::Bottom);
}

void MainComponent::SetPlaying(bool playing) {
    if (isPlaying_ == playing) {
        return;
    }

    isPlaying_ = playing;
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
    if (const auto selection = interactions_.takeSelectionChange())
        hierarchyPanel_.SelectEntity(*selection);
    if (pendingSceneTransitionId_.isNotEmpty()) {
        const auto requestedScene = pendingSceneTransitionId_;
        pendingSceneTransitionId_.clear();
        const bool resumeAfterTransition = isPlaying_;
        if (resumeAfterTransition) SetPlaying(false);
        selectScene(requestedScene);
        if (resumeAfterTransition) SetPlaying(true);
    }
    if (isPlaying_) {
        // GS6: runs every attached ScriptComponent's on_tick (and
        // on_start, on an entity's first playing tick) before advancing
        // World's tick counter -- the same Simulation::Step
        // CreationEngineServer's main loop calls, so the editor and
        // server genuinely execute scripts identically. 1/30s matches
        // this timer's own 30 Hz rate (startTimerHz(30) below).
        ce::engine::Simulation::Step(world_, 1.0f / 30.0f);
        ce::engine::FoundationGameplay::Step(world_, {}, 1.0f / 30.0f);
        frustHost_.tick(static_cast<std::int64_t>(world_.CurrentTick()));
    }
    tickLabel_.setText("tick " + juce::String(world_.CurrentTick()), juce::dontSendNotification);
    hierarchyPanel_.Refresh();
    transformPanel_.Refresh();
    pbrMaterialPanel_.Refresh();
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
    importPanel_.SetProjectContent(&projectSession_, activeGame_.assetRoot());
    if (!ce::project::EngineGameDocumentStore::loadScene(projectSession_, activeGame_, activeScene_, world_, errorMessage)) return false;
    viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
    frustHost_.prepareLevel(static_cast<std::int64_t>(world_.CurrentTick()));
    hierarchyPanel_.Refresh();
    transformPanel_.Refresh();
    pbrMaterialPanel_.Refresh();
    games_ = games;
    refreshGameScenePanel();
    headerBar_.setProjectLabel("Project: " + projectSession_.getManifest().projectName + " | " + activeGame_.name + " / " + activeScene_.name);
    saveAppSettings();
    return true;
}

void MainComponent::refreshGameScenePanel()
{
    gameScenePanel_.setDocuments(games_, activeGame_.id, activeScene_.id);
    gameScenePanel_.setStatus(activeGame_.id.isEmpty() ? "No active game" : activeGame_.name + " / " + activeScene_.name);
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
        importPanel_.SetProjectContent(&projectSession_, activeGame_.assetRoot());
        juce::String error;
        if (!ce::project::EngineGameDocumentStore::loadScene(projectSession_, activeGame_, activeScene_, world_, error))
            headerBar_.setStatusText("Could not open game: " + error);
        else {
            viewport_.ResolveProjectAssets(projectSession_, suiteSettings_);
            frustHost_.prepareLevel(static_cast<std::int64_t>(world_.CurrentTick()));
            refreshGameScenePanel();
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
            refreshGameScenePanel();
            saveAppSettings();
        }
        return;
    }
}

void MainComponent::createGame()
{
    auto* dialog = new juce::AlertWindow("New Game", "Name the game to add to this Suite project.", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", "New Game");
    dialog->addButton("Create", 1);
    dialog->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, dialog](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(dialog);
        if (result != 1 || safeThis == nullptr) return;
        ce::project::GameDocumentInfo game;
        ce::project::SceneDocumentInfo scene;
        juce::String error;
        if (!ce::project::EngineGameDocumentStore::createGame(safeThis->projectSession_, owned->getTextEditorContents("name"),
                                                               game, scene, error) ||
            !safeThis->projectSession_.commit(error)) {
            safeThis->headerBar_.setStatusText("Could not create game: " + error);
            return;
        }
        safeThis->games_.add(game);
        safeThis->activeGame_ = game;
        safeThis->activeScene_ = scene;
        safeThis->importPanel_.SetProjectContent(&safeThis->projectSession_, safeThis->activeGame_.assetRoot());
        safeThis->refreshGameScenePanel();
        safeThis->saveAppSettings();
        safeThis->headerBar_.setStatusText("Created " + game.name + " / " + scene.name);
    }), true);
}

void MainComponent::createScene()
{
    if (activeGame_.id.isEmpty()) return;
    auto* dialog = new juce::AlertWindow("New Scene", "Name the scene to add to this game.", juce::MessageBoxIconType::QuestionIcon);
    dialog->addTextEditor("name", "New Scene");
    dialog->addButton("Create", 1);
    dialog->addButton("Cancel", 0);
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, dialog](int result) mutable {
        std::unique_ptr<juce::AlertWindow> owned(dialog);
        if (result != 1 || safeThis == nullptr) return;
        ce::project::SceneDocumentInfo scene;
        juce::String error;
        if (!ce::project::EngineGameDocumentStore::createScene(safeThis->projectSession_, safeThis->activeGame_,
                                                                owned->getTextEditorContents("name"), scene, error) ||
            !safeThis->projectSession_.commit(error)) {
            safeThis->headerBar_.setStatusText("Could not create scene: " + error);
            return;
        }
        for (auto& game : safeThis->games_)
            if (game.id == safeThis->activeGame_.id) game = safeThis->activeGame_;
        safeThis->activeScene_ = scene;
        safeThis->refreshGameScenePanel();
        safeThis->saveAppSettings();
        safeThis->headerBar_.setStatusText("Created scene: " + scene.name);
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
                if (!openActiveGame(errorMessage)) return false;
                return true;
            }
        }
    }

    auto availableProjects = creation::assets::ProjectContainerService::listProjects(
        suiteSettings_, creation::assets::SuiteAppDomain::engine, errorMessage);

    if (! availableProjects.isEmpty())
    {
        if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings_, availableProjects.getFirst().projectId, projectSession_, errorMessage))
        {
            headerBar_.setProjectLabel("Project: " + projectSession_.getManifest().projectName);
            if (!openActiveGame(errorMessage)) return false;
            return true;
        }
    }

    createNewProject();
    return false;
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
