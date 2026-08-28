#include "MainComponent.h"

#include <creation/services/SuiteVfsJsonStore.h>

#include <creation/ui/CreationSuiteLogos.h>
#include "Scene/EngineSceneSerializer.h"

namespace {
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
    : viewport_(world_),
      hierarchyPanel_(world_, viewport_),
      transformPanel_(world_),
      pbrMaterialPanel_(world_),
      importPanel_(world_, viewport_),
      lightPanel_(viewport_) {
    juce::String suiteErr;
    suiteSettings_ = suiteSettingsStore_.load(suiteErr);

    std::string frustError;
    if (!frustHost_.loadBundled(frustError)) {
        juce::Logger::writeToLog("Creation Engine FRust host: " + juce::String(frustError));
    }
    frustAutomationPanel_ = std::make_unique<ce::views::FrustLogicPanel>(frustHost_.nodeLibraries());

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
        viewport_.ResetDemoEntityTransform();
        headerBar_.setStatusText("Stopped");
    };
    headerBar_.setStatusText("Editing");

    addAndMakeVisible(viewModeBar_);
    viewModeBar_.onModeSelected = [this](ce::WorkspaceMode mode) { SetActiveMode(mode); };
    runGameButton_.onClick = [this] { openGameClient(); };
    runGameButton_.setTooltip("Open an isolated game client window. Click again for another local multiplayer client.");
    addAndMakeVisible(runGameButton_);

    hierarchyPanel_.onSelectionChanged = [this](entt::entity entity) {
        transformPanel_.SetSelectedEntity(entity);
        pbrMaterialPanel_.SetSelectedEntity(entity);
    };
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
}

MainComponent::~MainComponent() {
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

void MainComponent::openGameClient()
{
    const int clientNumber = static_cast<int>(gameClients_.size()) + 1;
    gameClients_.push_back(std::make_unique<ce::runtime::GameClientWindow>(clientNumber));
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
    if (isPlaying_) {
        // GS6: runs every attached ScriptComponent's on_tick (and
        // on_start, on an entity's first playing tick) before advancing
        // World's tick counter -- the same Simulation::Step
        // CreationEngineServer's main loop calls, so the editor and
        // server genuinely execute scripts identically. 1/30s matches
        // this timer's own 30 Hz rate (startTimerHz(30) below).
        ce::engine::Simulation::Step(world_, 1.0f / 30.0f);
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
        options->saveSessionToDisk(true);
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
    loadSessionFromDisk();
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

    auto state = ce::scene::EngineSceneSerializer::serializeScene(world_);

    if (auto xml = state.createXml())
    {
        auto xmlString = xml->toString();
        juce::MemoryBlock xmlBlock(xmlString.toRawUTF8(), xmlString.getNumBytesAsUTF8());
        projectSession_.writeEntry("session.xml", xmlBlock);
    }

    juce::String commitError;
    if (! projectSession_.commit(commitError))
    {
        headerBar_.setStatusText("Project save failed: " + commitError);
        return;
    }

    if (userInitiated)
        headerBar_.setStatusText("Project saved: " + projectSession_.getManifest().projectName);
}

void MainComponent::loadSessionFromDisk()
{
    if (! projectSession_.isValid())
        return;

    juce::MemoryBlock sessionData;
    if (projectSession_.readEntry("session.xml", sessionData))
    {
        auto xmlString = juce::String::createStringFromData(sessionData.getData(), (int) sessionData.getSize());
        if (auto xml = juce::XmlDocument::parse(xmlString))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            if (ce::scene::EngineSceneSerializer::restoreScene(world_, state))
            {
                frustHost_.prepareLevel(static_cast<std::int64_t>(world_.CurrentTick()));
                hierarchyPanel_.Refresh();
                transformPanel_.Refresh();
                pbrMaterialPanel_.Refresh();
            }
        }
    }
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
                loadSessionFromDisk();
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
            loadSessionFromDisk();
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

    juce::String errorMessage;
    creation::services::SuiteVfsJsonStore::saveJson("engine-settings.json", juce::var(object), errorMessage);
}

void MainComponent::loadAppSettings()
{
    // The actual restore (last-opened project) happens inline in ensureProjectSessionActive(),
    // which is where it's actually needed (before deciding whether to create a new project) --
    // this function exists so save/load read as a matched pair, same shape as CreationStation's.
}
