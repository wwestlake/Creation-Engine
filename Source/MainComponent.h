#pragma once

#include <JuceHeader.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
#include <creation/ui/SuiteShellController.h>
#include <CreationDock/DockManager.h>

#include "engine/simulation.h"
#include "engine/world.h"
#include "Frust/EngineFrustHost.h"
#include "Interaction/EditorInteraction.h"
#include "Render/ViewportComponent.h"
#include "Views/HierarchyPanel.h"
#include "Views/ImportPanel.h"
#include "Views/FrustLogicPanel.h"
#include "Views/LightPanel.h"
#include "Views/MaterialsPanel.h"
#include "Views/PlaceholderPanel.h"
#include "Views/TransformPanel.h"
#include "Views/ViewModeBar.h"
#include "Views/GameScenePanel.h"
#include "Runtime/GameClientWindow.h"
#include "Project/EngineGameDocument.h"

#include <creation/assets/ProjectSession.h>
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/suite/SuiteSettings.h>

// The editor and the runtime are the same executable in different modes
// (capabilities spec, section 1). The top-level shell (TransportBar +
// ViewModeBar) mirrors Creation Station's — same color scheme, same tab-
// row/transport-bar structure — with Creation Engine's own tabs (Scene/
// Materials/Assets/Server/Settings) and a Play/Pause/Stop transport for
// the simulation itself (spec 3.3's "play-in-viewport") rather than an
// audio transport. Only Scene has a real panel today; the rest are
// PlaceholderPanel stand-ins until their own milestones land.
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            public juce::ApplicationCommandTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const juce::ApplicationCommandTarget::InvocationInfo& info) override;

private:
    void timerCallback() override;
    void SetActiveMode(ce::WorkspaceMode mode);
    void SetPlaying(bool playing);
    void initialiseDockingWorkspace();
    void openGameClient();

    void createNewProject();
    void openProject(const juce::String& projectId);
    void saveSessionToDisk(bool userInitiated = false);
    void loadSessionFromDisk();
    bool openActiveGame(juce::String& errorMessage);
    void selectGame(const juce::String& gameId);
    void selectScene(const juce::String& sceneId);
    void createGame();
    void createScene();
    void refreshGameScenePanel();
    bool ensureProjectSessionActive(juce::String& errorMessage);
    void saveAppSettings();
    void loadAppSettings();

    creation::suite::SuiteSettings suiteSettings_;
    creation::suite::SuiteSettingsStore suiteSettingsStore_;
    creation::assets::ProjectSession projectSession_;
    ce::project::GameDocumentInfo activeGame_;
    ce::project::SceneDocumentInfo activeScene_;
    juce::String pendingSceneTransitionId_;
    juce::Array<ce::project::GameDocumentInfo> games_;
    bool projectDirty_ = false;
    juce::ApplicationCommandManager commandManager_;

    ce::engine::World world_;
    ce::interaction::EditorInteraction interactions_ { world_ };
    ce::frust::EngineFrustHost frustHost_ { world_ };
    bool isPlaying_ = false;

    CreationSuiteHeaderBar headerBar_;
    creation::ui::SuiteShellController suiteShellController_;
    ce::ViewModeBar viewModeBar_;
    ce::WorkspaceMode activeMode_ = ce::WorkspaceMode::Scene;

    // --- Scene mode content ---
    // viewport_ must be declared (and therefore constructed) before
    // hierarchyPanel_: HierarchyPanel now takes a ViewportComponent&
    // (SC5's "+ Add" menu reads the asset catalog and camera position
    // through it), and member init order follows declaration order, not
    // the constructor's initializer-list order.
    ce::ViewportComponent viewport_;
    ce::HierarchyPanel hierarchyPanel_;
    ce::views::GameScenePanel gameScenePanel_;
    juce::Label inspectorTitle_ { {}, "Inspector" };
    juce::Label tickLabel_;
    juce::TextButton runGameButton_ { "Run Game Client" };

    ce::TransformPanel transformPanel_;

    // Selection-driven per-entity PBR editor (albedo/metallic/roughness),
    // replacing the old viewport-global roughness/metallic slider pair.
    // Not to be confused with materialsPanel_ below (the WorkspaceMode::
    // Materials tab's future node-based material editor).
    ce::MaterialsPanel pbrMaterialPanel_;

    ce::LightPanel lightPanel_;

    // AI1: Import Hub -- real panel, not a placeholder. Declared after
    // viewport_ (like hierarchyPanel_ above) since its constructor also
    // needs a fully-constructed ViewportComponent&.
    ce::ImportPanel importPanel_;

    // --- Other modes: stand-ins until their milestones land ---
    ce::PlaceholderPanel materialsPanel_ { "Materials", "Node-based material editor - coming soon" };
    std::unique_ptr<ce::views::FrustLogicPanel> frustAutomationPanel_;
    ce::PlaceholderPanel serverPanel_ { "Server", "Dedicated server operational view - coming soon" };
    ce::PlaceholderPanel settingsPanel_ { "Settings", "Application settings - coming soon" };

    std::unique_ptr<CreationDock::DockManager> dockManager_;
    std::vector<std::unique_ptr<ce::runtime::GameClientWindow>> gameClients_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
