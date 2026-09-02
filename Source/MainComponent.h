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
#include "Views/ContentBrowserPanel.h"
#include "Views/HierarchyPanel.h"
#include "Views/ImportPanel.h"
#include "Views/FrustLogicPanel.h"
#include "Views/LightPanel.h"
#include "Views/MaterialGraphPanel.h"
#include "Views/MaterialsPanel.h"
#include "Views/PlaceholderPanel.h"
#include "Views/TransformPanel.h"
#include "Views/ExplorerPanel.h"
#include "Runtime/GameClientWindow.h"
#include "Project/EngineGameDocument.h"

#include <creation/assets/ProjectSession.h>
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/suite/SuiteSettings.h>

// The editor and the runtime are the same executable in different modes
// (capabilities spec, section 1). The top-level shell is a header bar, a
// real menu bar (File/Edit/View/Project/Help, mirroring Creation Station's
// MainComponent-as-MenuBarModel pattern), and the docking workspace itself
// — switching between Scene/Logic/Materials/Assets/Server/Settings is just
// clicking that panel's dock tab or picking it from the View menu; there is
// no separate mode-tab row. A Play/Pause/Stop transport on the header bar
// drives the simulation itself (spec 3.3's "play-in-viewport"). Not every
// dock panel has a real editor yet; some are still PlaceholderPanel
// stand-ins until their own milestones land.
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            public juce::ApplicationCommandTarget,
                            private juce::MenuBarModel,
                            private juce::ComponentListener
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

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    // Keeps viewportRenderHost_ positioned over viewport_'s own on-screen
    // bounds while Scene Viewport is the active dock tab, and moved
    // off-canvas (same size, parked far outside the window) the rest of
    // the time. See viewportRenderHost_'s own comment for why: JUCE's GL
    // compositing for an attached context renders as its own layer that
    // ignores normal 2D paint occlusion (confirmed empirically -- painting
    // an opaque background over it from a sibling/parent component does
    // NOT hide it), so the only way to keep it always-alive (never
    // setVisible(false), which tears down the context) while also not
    // visually bleeding over whichever other CenterTab is actually active
    // is to move it, not hide it.
    void componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized) override;
    void componentVisibilityChanged(juce::Component& component) override;
    void syncViewportRenderHost();

    void timerCallback() override;
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
    void refreshExplorerPanel();
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
    std::unique_ptr<juce::MenuBarComponent> menuBar_;

    // --- Scene mode content ---
    // Always-alive host for the 3D viewport's juce::OpenGLContext, living
    // outside the dock tree entirely (a plain child of MainComponent, sent
    // toBack() and *never* setVisible(false)'d) so the context is never
    // torn down by switching away from the Scene Viewport dock tab -- a
    // real, reproduced crash otherwise (stale GPU handles from a recreated
    // context; see apps/CreationEngine/AGENTS.md's Do It Right Rule for why
    // this is the real fix rather than a per-panel workaround).
    //
    // Its bounds are kept in sync with viewport_'s own on-screen position
    // while Scene Viewport is the active tab, and moved off-canvas
    // (syncViewportRenderHost()) the rest of the time -- position, not
    // visibility, is what hides it, because JUCE's GL compositing for an
    // attached context turned out (confirmed by testing, not assumed) to
    // ignore normal 2D paint occlusion: a sibling/parent painting an opaque
    // background over this component's screen region does not hide its
    // rendered output. Must be declared (and constructed) before viewport_,
    // whose constructor takes a reference to it.
    juce::Component viewportRenderHost_;

    // viewport_ must be declared (and therefore constructed) before
    // hierarchyPanel_: HierarchyPanel now takes a ViewportComponent&
    // (SC5's "+ Add" menu reads the asset catalog and camera position
    // through it), and member init order follows declaration order, not
    // the constructor's initializer-list order.
    ce::ViewportComponent viewport_;
    ce::HierarchyPanel hierarchyPanel_;
    ce::views::ExplorerPanel explorerPanel_;
    juce::Label inspectorTitle_ { {}, "Inspector" };
    juce::Label tickLabel_;
    juce::TextButton runGameButton_ { "Run Game Client" };

    ce::TransformPanel transformPanel_;

    // Selection-driven per-entity PBR editor (albedo/metallic/roughness),
    // replacing the old viewport-global roughness/metallic slider pair.
    // Not to be confused with materialsPanel_ below (the "Materials" dock
    // panel's future node-based material editor).
    ce::MaterialsPanel pbrMaterialPanel_;

    ce::LightPanel lightPanel_;

    // AI1: Import Hub -- real panel, not a placeholder. Declared after
    // viewport_ (like hierarchyPanel_ above) since its constructor also
    // needs a fully-constructed ViewportComponent&.
    ce::ImportPanel importPanel_;

    // The real "Materials" dock panel -- a node graph editor over
    // ce::material's compiler (see MaterialGraphPanel.h). Declared after
    // viewport_ for the same reason as importPanel_ above: its
    // constructor needs a fully-constructed ViewportComponent& to reach
    // the asset catalog it applies compiled materials to.
    ce::views::MaterialGraphPanel materialsPanel_;

    // Tool-local content browser (models/textures/audio this project has
    // imported) -- see docs/architecture/Suite-Asset-Pipeline-Model.md.
    // Declared after importPanel_ for the same reason materialsPanel_ is
    // declared after viewport_: its constructor needs a fully-constructed
    // ImportPanel& to reach the AudioCatalog it evicts from on delete.
    ce::views::ContentBrowserPanel contentBrowserPanel_;

    // --- Other modes: stand-ins until their milestones land ---
    std::unique_ptr<ce::views::FrustLogicPanel> frustAutomationPanel_;
    ce::PlaceholderPanel serverPanel_ { "Server", "Dedicated server operational view - coming soon" };
    ce::PlaceholderPanel settingsPanel_ { "Settings", "Application settings - coming soon" };

    std::unique_ptr<CreationDock::DockManager> dockManager_;
    std::vector<std::unique_ptr<ce::runtime::GameClientWindow>> gameClients_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
