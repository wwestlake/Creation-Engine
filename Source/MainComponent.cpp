#include "MainComponent.h"

#include <creation/services/SuiteVfsJsonStore.h>

#if CE_ENABLE_SCRIPTING
#include "lang/jit/script_runtime.h"
#endif
#include <creation/ui/CreationSuiteLogos.h>
#include "Scene/EngineSceneSerializer.h"

namespace
{
// Wraps an existing component (not owned) as a dock panel's content, filling
// whatever bounds the dock zone/tab gives it.
class NonOwningPanelHost final : public juce::Component
{
public:
    explicit NonOwningPanelHost(juce::Component& contentToHost) : content(contentToHost)
    {
        addAndMakeVisible(content);
    }

    void resized() override
    {
        content.setBounds(getLocalBounds());
    }

private:
    juce::Component& content;
};

// The Scene-mode inspector column (title + tick counter + four per-entity
// editors) used to be laid out as one stacked block inside
// MainComponent::resized(). Registering all six as separate dock panels would
// be a much bigger decomposition than this pass is scoping (see the rollout
// plan) -- this keeps them as one dock panel, with the exact same stacked
// layout math lifted verbatim out of the old MainComponent::resized().
class EngineInspectorHost final : public juce::Component
{
public:
    EngineInspectorHost(juce::Label& inspectorTitle, juce::Label& tickLabel, ce::TransformPanel& transformPanel,
                        ce::ScriptPanel& scriptPanel, ce::MaterialsPanel& pbrMaterialPanel, ce::LightPanel& lightPanel)
        : inspectorTitle_(inspectorTitle), tickLabel_(tickLabel), transformPanel_(transformPanel),
          scriptPanel_(scriptPanel), pbrMaterialPanel_(pbrMaterialPanel), lightPanel_(lightPanel)
    {
        addAndMakeVisible(inspectorTitle_);
        addAndMakeVisible(tickLabel_);
        addAndMakeVisible(transformPanel_);
        addAndMakeVisible(scriptPanel_);
        addAndMakeVisible(pbrMaterialPanel_);
        addAndMakeVisible(lightPanel_);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(12);

        inspectorTitle_.setBounds(bounds.removeFromTop(28));
        tickLabel_.setBounds(bounds.removeFromTop(24));

        bounds.removeFromTop(12);
        transformPanel_.setBounds(bounds.removeFromTop(ce::TransformPanel::kPreferredHeight));

        bounds.removeFromTop(12);
        scriptPanel_.setBounds(bounds.removeFromTop(ce::ScriptPanel::kPreferredHeight));

        bounds.removeFromTop(12);
        pbrMaterialPanel_.setBounds(bounds.removeFromTop(ce::MaterialsPanel::kPreferredHeight));

        bounds.removeFromTop(16);
        lightPanel_.setBounds(bounds.removeFromTop(lightPanel_.PreferredHeight()));
    }

private:
    juce::Label& inspectorTitle_;
    juce::Label& tickLabel_;
    ce::TransformPanel& transformPanel_;
    ce::ScriptPanel& scriptPanel_;
    ce::MaterialsPanel& pbrMaterialPanel_;
    ce::LightPanel& lightPanel_;
};

const juce::String panelIdHierarchy = "hierarchy";
const juce::String panelIdViewport = "viewport";
const juce::String panelIdInspector = "inspector";
const juce::String panelIdLogic = "logic";
const juce::String panelIdAssets = "assets";
const juce::String panelIdMaterials = "materials";
const juce::String panelIdServer = "server";
const juce::String panelIdSettings = "settings";

constexpr int menuIdPanelHierarchy = 3001;
constexpr int menuIdPanelViewport = 3002;
constexpr int menuIdPanelInspector = 3003;
constexpr int menuIdPanelLogic = 3004;
constexpr int menuIdPanelAssets = 3005;
constexpr int menuIdPanelMaterials = 3006;
constexpr int menuIdPanelServer = 3007;
constexpr int menuIdPanelSettings = 3008;
constexpr int menuIdResetLayout = 3009;
}

MainComponent::MainComponent()
    : viewport_(world_),
      hierarchyPanel_(world_, viewport_),
      transformPanel_(world_),
      scriptPanel_(world_, viewport_),
      pbrMaterialPanel_(world_),
      importPanel_(world_, viewport_),
      lightPanel_(viewport_),
      logicPanel_(world_) {
    juce::String suiteErr;
    suiteSettings_ = suiteSettingsStore_.load(suiteErr);

#if CE_ENABLE_SCRIPTING
    world_.SetScriptRuntime(ce::lang::jit::CreateScriptRuntime());
#endif

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
                                     creation::ui::SuiteAssetManagerCapability{ "Creation Engine", creation::assets::SuiteAppDomain::engine, { ".cel" }, { ".cel" } }
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

    hierarchyPanel_.onSelectionChanged = [this](entt::entity entity) {
        transformPanel_.SetSelectedEntity(entity);
        scriptPanel_.SetSelectedEntity(entity);
        pbrMaterialPanel_.SetSelectedEntity(entity);
        logicPanel_.SetSelectedEntity(entity);
    };

    inspectorTitle_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    inspectorTitle_.setColour(juce::Label::textColourId, juce::Colours::white);

    tickLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    // hierarchyPanel_/viewport_/inspectorTitle_/tickLabel_/transformPanel_/
    // scriptPanel_/pbrMaterialPanel_/lightPanel_/materialsPanel_/importPanel_/
    // logicPanel_/serverPanel_/settingsPanel_ are all reparented into dock
    // panels below (see initialiseDockingWorkspace), not added directly here.

    SetPlaying(false);

    menuBar_ = std::make_unique<juce::MenuBarComponent>(static_cast<juce::MenuBarModel*>(this));
    // Nothing in this app sets a suite-wide dark LookAndFeel, so MenuBarComponent
    // falls back to LookAndFeel_V4::drawMenuBarItem/drawMenuBarBackground, which key
    // off TextButton colour ids (not PopupMenu's) -- the default scheme renders dark
    // text on a dark bar, invisible against this app's dark theme without this.
    menuBar_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c2230));
    menuBar_->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a3244));
    menuBar_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    menuBar_->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(*menuBar_);

    dockManager_ = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager_);
    initialiseDockingWorkspace();
    // setSize() below fires resized() immediately; menuBar_/dockManager_ must
    // already exist and be registered before that happens, or they're silently
    // left at zero bounds (addAndMakeVisible alone doesn't trigger a layout pass).
    resized();

    setSize(1400, 900);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

void MainComponent::resized() {
    auto bounds = getLocalBounds();

    headerBar_.setBounds(bounds.removeFromTop(96));
    viewModeBar_.setBounds(bounds.removeFromTop(56));

    if (menuBar_ != nullptr)
        menuBar_->setBounds(bounds.removeFromTop(28));

    if (dockManager_ != nullptr)
        dockManager_->setBounds(bounds);
}

// ViewModeBar is kept as a quick-nav affordance now that panels are
// independent dock toggles rather than an exclusive-mode switch (see the
// rollout plan) -- clicking a mode tab opens/activates that mode's panel(s)
// instead of hiding every other mode's panels.
void MainComponent::SetActiveMode(ce::WorkspaceMode mode) {
    activeMode_ = mode;

    if (dockManager_ == nullptr)
        return;

    const auto jumpTo = [this](const juce::String& panelId, CreationDock::DockTargetZone fallbackZone) {
        if (! dockManager_->isPanelOpen(panelId))
            dockManager_->showPanel(panelId, fallbackZone);
        else
            dockManager_->activatePanel(panelId);
    };

    switch (mode) {
        case ce::WorkspaceMode::Scene:
            jumpTo(panelIdHierarchy, CreationDock::DockTargetZone::Left);
            jumpTo(panelIdInspector, CreationDock::DockTargetZone::Right);
            jumpTo(panelIdViewport, CreationDock::DockTargetZone::CenterTab);
            break;
        case ce::WorkspaceMode::Logic:      jumpTo(panelIdLogic, CreationDock::DockTargetZone::CenterTab); break;
        case ce::WorkspaceMode::Materials:  jumpTo(panelIdMaterials, CreationDock::DockTargetZone::CenterTab); break;
        case ce::WorkspaceMode::Assets:     jumpTo(panelIdAssets, CreationDock::DockTargetZone::CenterTab); break;
        case ce::WorkspaceMode::Server:     jumpTo(panelIdServer, CreationDock::DockTargetZone::CenterTab); break;
        case ce::WorkspaceMode::Settings:   jumpTo(panelIdSettings, CreationDock::DockTargetZone::CenterTab); break;
    }

    menuItemsChanged();
}

juce::StringArray MainComponent::getMenuBarNames() {
    return { "Panels" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int, const juce::String&) {
    juce::PopupMenu menu;

    const auto isOpen = [this](const juce::String& id) {
        return dockManager_ != nullptr && dockManager_->isPanelOpen(id);
    };

    menu.addItem(menuIdPanelHierarchy, "Hierarchy", true, isOpen(panelIdHierarchy));
    menu.addItem(menuIdPanelViewport, "Viewport", true, isOpen(panelIdViewport));
    menu.addItem(menuIdPanelInspector, "Inspector", true, isOpen(panelIdInspector));
    menu.addItem(menuIdPanelLogic, "Logic", true, isOpen(panelIdLogic));
    menu.addItem(menuIdPanelAssets, "Assets", true, isOpen(panelIdAssets));
    menu.addItem(menuIdPanelMaterials, "Materials", true, isOpen(panelIdMaterials));
    menu.addItem(menuIdPanelServer, "Server", true, isOpen(panelIdServer));
    menu.addItem(menuIdPanelSettings, "Settings", true, isOpen(panelIdSettings));
    menu.addSeparator();
    menu.addItem(menuIdResetLayout, "Reset Dock Layout");
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int) {
    switch (menuItemID) {
        case menuIdPanelHierarchy: toggleDockPanel(panelIdHierarchy, CreationDock::DockTargetZone::Left); break;
        case menuIdPanelViewport:  toggleDockPanel(panelIdViewport, CreationDock::DockTargetZone::CenterTab); break;
        case menuIdPanelInspector: toggleDockPanel(panelIdInspector, CreationDock::DockTargetZone::Right); break;
        case menuIdPanelLogic:     toggleDockPanel(panelIdLogic, CreationDock::DockTargetZone::CenterTab); break;
        case menuIdPanelAssets:    toggleDockPanel(panelIdAssets, CreationDock::DockTargetZone::CenterTab); break;
        case menuIdPanelMaterials: toggleDockPanel(panelIdMaterials, CreationDock::DockTargetZone::CenterTab); break;
        case menuIdPanelServer:    toggleDockPanel(panelIdServer, CreationDock::DockTargetZone::CenterTab); break;
        case menuIdPanelSettings:  toggleDockPanel(panelIdSettings, CreationDock::DockTargetZone::CenterTab); break;
        case menuIdResetLayout:    if (dockManager_ != nullptr) dockManager_->resetLayout(); break;
        default: break;
    }

    menuItemsChanged();
}

void MainComponent::initialiseDockingWorkspace() {
    if (dockManager_ == nullptr)
        return;

    inspectorHost_ = std::make_unique<EngineInspectorHost>(inspectorTitle_, tickLabel_, transformPanel_,
                                                            scriptPanel_, pbrMaterialPanel_, lightPanel_);

    dockManager_->registerPanel(panelIdHierarchy, "Hierarchy",
        std::make_unique<NonOwningPanelHost>(hierarchyPanel_), CreationDock::DockTargetZone::Left);
    dockManager_->registerPanel(panelIdViewport, "Viewport",
        std::make_unique<NonOwningPanelHost>(viewport_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel(panelIdInspector, "Inspector",
        std::make_unique<NonOwningPanelHost>(*inspectorHost_), CreationDock::DockTargetZone::Right);
    dockManager_->registerPanel(panelIdLogic, "Logic",
        std::make_unique<NonOwningPanelHost>(logicPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel(panelIdAssets, "Assets",
        std::make_unique<NonOwningPanelHost>(importPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel(panelIdMaterials, "Materials",
        std::make_unique<NonOwningPanelHost>(materialsPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel(panelIdServer, "Server",
        std::make_unique<NonOwningPanelHost>(serverPanel_), CreationDock::DockTargetZone::CenterTab);
    dockManager_->registerPanel(panelIdSettings, "Settings",
        std::make_unique<NonOwningPanelHost>(settingsPanel_), CreationDock::DockTargetZone::CenterTab);

    // Start with just the Scene trio open, matching the app's old default
    // (WorkspaceMode::Scene was the initial active mode) -- everything else
    // starts closed, reachable via the Panels menu or the ViewModeBar tabs.
    for (const auto& panelId : { panelIdLogic, panelIdAssets, panelIdMaterials, panelIdServer, panelIdSettings })
        dockManager_->closePanel(panelId);
}

void MainComponent::toggleDockPanel(const juce::String& panelId, CreationDock::DockTargetZone fallbackZone) {
    if (dockManager_ == nullptr)
        return;

    if (dockManager_->isPanelOpen(panelId))
        dockManager_->closePanel(panelId);
    else
        dockManager_->showPanel(panelId, fallbackZone);

    menuItemsChanged();
}

void MainComponent::SetPlaying(bool playing) {
    isPlaying_ = playing;
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
    }
    tickLabel_.setText("tick " + juce::String(world_.CurrentTick()), juce::dontSendNotification);
    hierarchyPanel_.Refresh();
    transformPanel_.Refresh();
    scriptPanel_.Refresh();
    pbrMaterialPanel_.Refresh();
    logicPanel_.Refresh();
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
                hierarchyPanel_.Refresh();
                transformPanel_.Refresh();
                scriptPanel_.Refresh();
                logicPanel_.Refresh();
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
