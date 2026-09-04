cmake_minimum_required(VERSION 3.22)

set(CREATION_ENGINE_VERSION "0.0.1" CACHE STRING "Creation Engine product version")
project(CreationEngine VERSION ${CREATION_ENGINE_VERSION} LANGUAGES C CXX)

# Each suite agent works from its own workspace, D:/CreationSuite-Workspaces/
# CreationSuite-<Agent>/apps/CreationEngine (see AGENTS.md's Multi-Agent
# Rule) -- derive <Agent> from that path rather than hardcoding one agent's
# name, so the POST_BUILD copy below always lands in the bin directory that
# actually matches whoever is building.
get_filename_component(CREATION_ENGINE_WORKSPACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(CREATION_ENGINE_WORKSPACE_NAME "${CREATION_ENGINE_WORKSPACE_DIR}" NAME)
string(REGEX REPLACE "^CreationSuite-" "" CREATION_ENGINE_AGENT_NAME "${CREATION_ENGINE_WORKSPACE_NAME}")
string(TOLOWER "${CREATION_ENGINE_AGENT_NAME}" CREATION_ENGINE_AGENT_NAME)
if (NOT CREATION_ENGINE_AGENT_NAME)
    set(CREATION_ENGINE_AGENT_NAME "dev")
endif()

# Shared across every suite agent workspace -- not vcpkg-managed, a single
# manually-provisioned OpenXR SDK all three workspaces are meant to build
# against. Passed as a HINTS path so a plain `cmake -S . -B build` finds it
# with no extra flags in any workspace; still overridable via
# -DCREATION_OPENXR_INCLUDE_DIR/-DCREATION_OPENXR_LOADER_LIBRARY if it's ever
# provisioned somewhere else.
set(CREATION_OPENXR_SHARED_DEPS_ROOT "D:/CreationSuite-Workspaces/openxr-deps/x64-windows")
find_path(CREATION_OPENXR_INCLUDE_DIR NAMES openxr/openxr.h
    HINTS "${CREATION_OPENXR_SHARED_DEPS_ROOT}/include")
find_library(CREATION_OPENXR_LOADER_LIBRARY NAMES openxr_loader
    HINTS "${CREATION_OPENXR_SHARED_DEPS_ROOT}/lib")

# OpenXR/VR support is a required capability of Creation Engine, not an
# optional extra -- it must not silently disable itself when the shared SDK
# location isn't found. Fail loudly and say exactly what to fix, rather than
# quietly building a VR-incapable editor that looks fine until someone puts
# a headset on.
if (NOT CREATION_OPENXR_INCLUDE_DIR OR NOT CREATION_OPENXR_LOADER_LIBRARY)
    message(FATAL_ERROR
        "Creation Engine requires the OpenXR SDK and did not find it. "
        "Expected it at ${CREATION_OPENXR_SHARED_DEPS_ROOT} (shared across "
        "every suite agent workspace). If it has moved, reconfigure with "
        "-DCREATION_OPENXR_INCLUDE_DIR=<path>/include "
        "-DCREATION_OPENXR_LOADER_LIBRARY=<path>/lib/openxr_loader.lib.")
endif()
set(CREATION_ENGINE_HAS_OPENXR 1)
message(STATUS "Creation Engine: OpenXR runtime support enabled (${CREATION_OPENXR_INCLUDE_DIR})")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to your JUCE root directory before configuring (same JUCE checkout Creation Station uses).")
endif()

enable_testing()

add_subdirectory("${JUCE_DIR}" juce)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt/CMakeLists.txt")
    add_subdirectory(third_party/entt)
elseif(EXISTS "D:/000 Creation Engine/third_party/entt/CMakeLists.txt")
    add_subdirectory("D:/000 Creation Engine/third_party/entt" EnttExternal)
else()
    message(FATAL_ERROR "Could not find the entt dependency for Creation Engine.")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/JuceDocking/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/JuceDocking"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedJuceDocking")
endif()

# --- Shared, framework-agnostic libraries --------------------------------
# NodeSystem is language-neutral graph infrastructure, owned by the suite
# (docs/SHARED_EXTRACTION_PLAN.md) so every app -- Engine, Texture, and
# whatever else needs node-graph-authored content -- shares one substrate
# and one node catalog contract instead of drifting apart. Previously
# Engine kept its own forked copy under this directory; that fork is gone
# (wwestlake/Creation-Suite#115) and its FRust-specific pieces
# (frust_codegen, node_library) were carried over into the shared copy
# rather than left behind.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/NodeSystem" CreationSuiteSharedNodeSystem)
# MaterialSystem: the node-graph -> GLSL compiler, shared for the same
# reason NodeSystem is -- Creation Texture will produce the same kind of
# materials Engine does (docs/MATERIAL_SYSTEM_PLAN.md).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/MaterialSystem" CreationSuiteSharedMaterialSystem)
add_library(creation_engine_ufbx STATIC third_party/ufbx/ufbx.c)
target_include_directories(creation_engine_ufbx PUBLIC third_party/ufbx)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/NodeEditorUI/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/NodeEditorUI"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedNodeEditorUI")
endif()
add_subdirectory(EngineCore)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/VFS" CreationSuiteSharedVFS)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/AssetSystem" CreationSuiteSharedAssetSystem)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Services" CreationSuiteSharedServices)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI" CreationSuiteSharedUI)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/FrustPluginRuntime" CreationSuiteSharedFrustPluginRuntime)

# --- Shared, juce_core-only libraries -------------------------------------
# Not framework-agnostic like the two above (needs juce::juce_core for
# juce::ZipFile), but still no GUI/rendering dependency, so a future
# server-side asset loader could link this without pulling in anything
# beyond what CreationEngineServer already links.

# --- Client / editor executable ------------------------------------------
# Per spec section 1, the editor and the runtime are the same binary in
# different modes — not a separate desktop app talking to the game over a
# socket.
juce_add_gui_app(CreationEngineEditor
    PRODUCT_NAME "Creation Engine"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/icon.png"
)

# Mirrors the built executable into the workspace-level shared bin
# directory (D:\CreationSuite-Workspaces\<agent>-<config>-bin) so it's
# reachable at a stable path without needing OS symlink privileges.
# Every suite agent does this for its own repo -- see AGENTS.md.
add_custom_command(TARGET CreationEngineEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationEngineEditor>" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/EngineLifecycle.frust" "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodes.frust" "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodesLibrary.frust" "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins/"
    # The shared bin mirror above (line ~122) never got this same
    # plugins/ treatment EnginePack gets below -- meaning EngineFrustHost::
    # loadBundled() has been silently failing (plugins/EngineLifecycle.frust
    # "not found beside the executable") for every run launched from the
    # shared claude-debug-bin path, since that check requires ALL of
    # plugins/{EngineLifecycle,CoreNodes}.frust to exist right beside the
    # exe. Found while wiring FRust-driven material parameters -- the host
    # function was never being reached at all.
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/EngineLifecycle.frust" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodes.frust" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodesLibrary.frust" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/EnginePack" "$<TARGET_FILE_DIR:CreationEngineEditor>/EnginePack"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/EnginePack" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/EnginePack"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/models" "$<TARGET_FILE_DIR:CreationEngineEditor>/EnginePack/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/models" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/EnginePack/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/packages" "$<TARGET_FILE_DIR:CreationEngineEditor>/EnginePack/packages"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/packages" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/EnginePack/packages"
    COMMENT "Copying CreationEngineEditor to the shared ${CREATION_ENGINE_AGENT_NAME} bin directory"
)

juce_generate_juce_header(CreationEngineEditor)

target_sources(CreationEngineEditor PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/Project/EngineGameDocument.h
    Source/Project/EngineGameDocument.cpp
    Source/Project/StarterGameTemplates.h
    Source/Views/NewGameDialog.h
    Source/Views/NewGameDialog.cpp
    Source/Assets/EngineAssetPack.h
    Source/Assets/EngineAssetPack.cpp
    Source/Assets/AssetPackStore.h
    Source/Assets/AssetPackStore.cpp
    Source/Assets/ProjectContentAssetStore.h
    Source/Assets/ProjectContentAssetStore.cpp
    Source/Frust/PodCatalog.h
    Source/Frust/PodCatalog.cpp
    Source/Frust/EngineFrustHost.h
    Source/Frust/EngineFrustHost.cpp
    Source/Frust/EngineNodeLibraryLoader.h
    Source/Frust/EngineNodeLibraryLoader.cpp
    Source/Interaction/EditorInteraction.h
    Source/Interaction/EditorInteraction.cpp
    Source/Branding.h
    Source/Branding.cpp
    Source/Render/ViewportComponent.h
    Source/Render/ViewportComponent.cpp
    Source/Render/GL/Buffer.h
    Source/Render/GL/Buffer.cpp
    Source/Render/GL/VertexArray.h
    Source/Render/GL/VertexArray.cpp
    Source/Render/GL/Texture2D.h
    Source/Render/GL/Texture2D.cpp
    Source/Render/Scene/Camera.h
    Source/Render/Scene/Camera.cpp
    Source/Render/Scene/FreeCamera.h
    Source/Render/Scene/FreeCamera.cpp
    Source/Render/Scene/GridRenderer.h
    Source/Render/Scene/GridRenderer.cpp
    Source/Render/Scene/Vertex.h
    Source/Render/Scene/Mesh.h
    Source/Render/Scene/Mesh.cpp
    Source/Render/Scene/Material.h
    Source/Render/Scene/Material.cpp
    Source/Render/Scene/Animation.h
    Source/Render/Scene/ProceduralMesh.h
    Source/Render/Scene/ProceduralMesh.cpp
    Source/Render/Shaders/ShaderComposer.h
    Source/Render/Shaders/ShaderComposer.cpp
    Source/Render/Import/GltfLoader.h
    Source/Render/Import/GltfLoader.cpp
    Source/Render/Import/FbxLoader.h
    Source/Render/Import/FbxLoader.cpp
    Source/Render/Import/ObjLoader.h
    Source/Render/Import/ObjLoader.cpp
    Source/Render/Scene/Light.h
    Source/Views/LightPanel.h
    Source/Views/LightPanel.cpp
    Source/Views/TransportLookAndFeel.h
    Source/Views/TransportLookAndFeel.cpp
    Source/Views/TransportBar.h
    Source/Views/TransportBar.cpp
    Source/Views/PodEditorPanel.h
    Source/Views/PodEditorPanel.cpp
    Source/Views/PodInfoPanel.h
    Source/Views/PodInfoPanel.cpp
    Source/Views/ObjectDefinitionEditorPanel.h
    Source/Views/ObjectDefinitionEditorPanel.cpp
    Source/Views/TransformFieldEditor.h
    Source/Views/TransformFieldEditor.cpp
    Source/Views/LogPanel.h
    Source/Views/LogPanel.cpp
    Source/Diagnostics/EngineLog.h
    Source/Diagnostics/EngineLog.cpp
    Source/Diagnostics/JuceLoggerBridge.h
    Source/Diagnostics/JuceLoggerBridge.cpp
    Source/Diagnostics/EngineLogVfsWriter.h
    Source/Diagnostics/EngineLogVfsWriter.cpp
    Source/Views/BehaviorAttachmentPanel.h
    Source/Views/BehaviorAttachmentPanel.cpp
    Source/Runtime/GameClientWindow.h
    Source/Runtime/GameClientWindow.cpp
    Source/Character/CharacterDefinition.h
    Source/Character/CharacterDefinition.cpp
    Source/Views/PlaceholderPanel.h
    Source/Views/PlaceholderPanel.cpp
    Source/Views/HierarchyPanel.h
    Source/Views/HierarchyPanel.cpp
    Source/Views/ExplorerPanel.h
    Source/Views/ExplorerPanel.cpp
    Source/Views/TransformPanel.h
    Source/Views/TransformPanel.cpp
    Source/Views/MaterialsPanel.h
    Source/Views/MaterialsPanel.cpp
    Source/Views/MaterialGraphPanel.h
    Source/Views/MaterialGraphPanel.cpp
    Source/Views/ImportPanel.h
    Source/Views/ImportPanel.cpp
    Source/Views/ContentBrowserPanel.h
    Source/Views/ContentBrowserPanel.cpp
    Source/Scene/Components.h
    Source/Scene/EngineSceneSerializer.h
    Source/Scene/EngineSceneSerializer.cpp
    Source/Scene/ObjectDefinitions.h
    Source/Scene/ObjectDefinitions.cpp
    Source/Scene/ObjectDefinitionNaming.h
    Source/Scene/ObjectDefinitionNaming.cpp
    Source/Scene/AssetCatalog.h
    Source/Scene/AssetCatalog.cpp
    Source/Scene/AssetPlacement.h
    Source/Scene/AssetPlacement.cpp
    Source/Scene/AnimationSampler.h
    Source/Scene/AnimationSampler.cpp
    Source/Scene/AnimationSlicer.h
    Source/Scene/AnimationSlicer.cpp
    Source/Import/AssetImporter.h
    Source/Import/ImporterRegistry.h
    Source/Import/ImporterRegistry.cpp
    Source/Import/Importers/GltfAssetImporter.h
    Source/Import/Importers/GltfAssetImporter.cpp
    Source/Import/Importers/FbxAssetImporter.h
    Source/Import/Importers/FbxAssetImporter.cpp
    Source/Import/Importers/ObjAssetImporter.h
    Source/Import/Importers/ObjAssetImporter.cpp
    Source/Import/Importers/AudioAssetImporter.h
    Source/Import/Importers/AudioAssetImporter.cpp
    Source/Import/Importers/TextureAssetImporter.h
    Source/Import/Importers/TextureAssetImporter.cpp
    Source/Audio/AudioCatalog.h
    Source/Audio/AudioCatalog.cpp
    Source/Audio/AudioPreviewPlayer.h
    Source/Audio/AudioPreviewPlayer.cpp
    Source/VR/OpenXRProvider.h
    Source/VR/OpenXRProvider.cpp
)

target_include_directories(CreationEngineEditor PRIVATE
    Source
    third_party
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/JuceDocking"
)

if (CREATION_ENGINE_HAS_OPENXR)
    target_include_directories(CreationEngineEditor PRIVATE ${CREATION_OPENXR_INCLUDE_DIR})
    target_link_libraries(CreationEngineEditor PRIVATE ${CREATION_OPENXR_LOADER_LIBRARY})
endif()

target_compile_definitions(CreationEngineEditor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_OPENGL3=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationEngineEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationEngineEditor,JUCE_VERSION>"
    CE_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Source/Render/Shaders"
    CE_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    CE_HAS_OPENXR=${CREATION_ENGINE_HAS_OPENXR}
)

target_link_libraries(CreationEngineEditor PRIVATE
    engine_core
    node_system
    creation_suite_material_system
    creation_suite_asset_system
    creation_suite_services
    creation_suite_ui
    creation_suite_ui_assets
    creation_suite_frust_plugin_runtime
    creation_engine_ufbx
    CreationDock
    creation_suite_node_editor_ui
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

add_executable(CreationEngineFrustPluginSmoke
    tests/EngineFrustPluginSmoke.cpp
    Source/Frust/EngineFrustHost.cpp
    Source/Frust/EngineNodeLibraryLoader.cpp
)

if(MSVC)
    target_compile_options(CreationEngineEditor PRIVATE /MP1 /FS)
    # Make the desired incremental Debug-link behavior explicit. The editor is
    # large; normal iteration must never force a clean link for a one-file
    # source change.
    target_link_options(CreationEngineEditor PRIVATE "$<$<CONFIG:Debug>:/INCREMENTAL>")
endif()
target_include_directories(CreationEngineFrustPluginSmoke PRIVATE Source)
target_link_libraries(CreationEngineFrustPluginSmoke PRIVATE
    engine_core
    node_system
    creation_suite_frust_plugin_runtime
    juce::juce_core
)
target_compile_definitions(CreationEngineFrustPluginSmoke PRIVATE
    CE_ENGINE_LIFECYCLE_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/EngineLifecycle.frust"
)
add_test(NAME creation_engine_frust_plugin_smoke COMMAND CreationEngineFrustPluginSmoke)

add_executable(CreationEngineMultiPluginRuntimeSmoke
    tests/MultiPluginRuntimeSmoke.cpp
)
target_link_libraries(CreationEngineMultiPluginRuntimeSmoke PRIVATE
    creation_suite_frust_plugin_runtime
)
target_compile_definitions(CreationEngineMultiPluginRuntimeSmoke PRIVATE
    CE_RUNTIME_ALPHA_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/RuntimeAlpha.frust"
    CE_RUNTIME_BETA_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/RuntimeBeta.frust"
)
add_test(NAME creation_engine_multi_plugin_runtime_smoke COMMAND CreationEngineMultiPluginRuntimeSmoke)

add_executable(CreationEngineObjectBehaviorSmoke
    tests/EngineObjectBehaviorSmoke.cpp
    Source/Frust/EngineFrustHost.cpp
    Source/Frust/EngineNodeLibraryLoader.cpp
)
target_include_directories(CreationEngineObjectBehaviorSmoke PRIVATE Source)
target_link_libraries(CreationEngineObjectBehaviorSmoke PRIVATE
    engine_core
    node_system
    creation_suite_frust_plugin_runtime
    juce::juce_core
    juce::juce_graphics
    juce::juce_opengl
)
target_compile_definitions(CreationEngineObjectBehaviorSmoke PRIVATE
    CE_ENGINE_OBJECT_BEHAVIOR_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/SetCurrentObjectX.frust"
)
add_test(NAME creation_engine_object_behavior_smoke COMMAND CreationEngineObjectBehaviorSmoke)

add_executable(CreationEngineObjectDefinitionSmoke
    tests/ObjectDefinitionSmoke.cpp
    Source/Scene/ObjectDefinitions.cpp
)
target_include_directories(CreationEngineObjectDefinitionSmoke PRIVATE Source)
target_link_libraries(CreationEngineObjectDefinitionSmoke PRIVATE
    engine_core
    creation_suite_asset_system
    juce::juce_core
    juce::juce_graphics
    juce::juce_opengl
)
add_test(NAME creation_engine_object_definition_smoke COMMAND CreationEngineObjectDefinitionSmoke)

add_executable(CreationEngineFrustGraphSmoke
    tests/FrustGraphSmoke.cpp
)
target_link_libraries(CreationEngineFrustGraphSmoke PRIVATE
    node_system
)
add_test(NAME creation_engine_frust_graph_smoke COMMAND CreationEngineFrustGraphSmoke)

add_executable(CreationEngineNodeLibraryLoaderSmoke
    tests/EngineNodeLibraryLoaderSmoke.cpp
    Source/Frust/EngineNodeLibraryLoader.cpp
)
target_include_directories(CreationEngineNodeLibraryLoaderSmoke PRIVATE Source)
target_link_libraries(CreationEngineNodeLibraryLoaderSmoke PRIVATE
    node_system
    creation_suite_frust_plugin_runtime
    juce::juce_core
)
add_test(NAME creation_engine_node_library_loader_smoke COMMAND CreationEngineNodeLibraryLoaderSmoke)

add_executable(CreationEngineFrustCodegenSmoke
    tests/FrustCodegenSmoke.cpp
)
target_link_libraries(CreationEngineFrustCodegenSmoke PRIVATE node_system)
target_link_libraries(CreationEngineFrustCodegenSmoke PRIVATE creation_suite_frust_plugin_runtime)
target_compile_definitions(CreationEngineFrustCodegenSmoke PRIVATE
    CE_CORE_NODE_LIBRARY="${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodesLibrary.frust"
)
add_test(NAME creation_engine_frust_codegen_smoke COMMAND CreationEngineFrustCodegenSmoke)

# --- Dedicated server executable ------------------------------------------
# Headless build: no rendering or client-only assets. Only juce_core and
# juce_events provide cross-platform utilities for the server shell.
juce_add_console_app(CreationEngineServer
    PRODUCT_NAME "Creation Engine Server"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine.server"
)

# Mirrors the built executable into the workspace-level shared bin
# directory (D:\CreationSuite-Workspaces\<agent>-<config>-bin) so it's
# reachable at a stable path without needing OS symlink privileges.
# Every suite agent does this for its own repo -- see AGENTS.md.
add_custom_command(TARGET CreationEngineServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationEngineServer>" "D:/CreationSuite-Workspaces/${CREATION_ENGINE_AGENT_NAME}-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMENT "Copying CreationEngineServer to the shared ${CREATION_ENGINE_AGENT_NAME} bin directory"
)

juce_generate_juce_header(CreationEngineServer)

target_sources(CreationEngineServer PRIVATE
    Server/Source/Main.cpp
    Source/Project/EngineGameDocument.h
    Source/Project/EngineGameDocument.cpp
    Source/Assets/EngineAssetPack.h
    Source/Assets/EngineAssetPack.cpp
    Source/Assets/AssetPackStore.h
    Source/Assets/AssetPackStore.cpp
    Source/Scene/EngineSceneSerializer.h
    Source/Scene/EngineSceneSerializer.cpp
)
target_include_directories(CreationEngineServer PRIVATE Source)

target_compile_definitions(CreationEngineServer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationEngineServer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationEngineServer,JUCE_VERSION>"
)

target_link_libraries(CreationEngineServer PRIVATE
    engine_core
    node_system
    creation_suite_asset_system
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

install(TARGETS CreationEngineEditor
    RUNTIME DESTINATION .
    RESOURCE DESTINATION .
)
install(FILES README.md LICENSE DESTINATION .)

set(CPACK_PACKAGE_NAME "Creation Engine")
set(CPACK_PACKAGE_VENDOR "LagDaemon Software")
set(CPACK_PACKAGE_CONTACT "LagDaemon Software")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Creation Engine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "creation-engine-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Creation Engine")
set(CPACK_GENERATOR "ZIP;WIX")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Creation Engine")
set(CPACK_PACKAGE_EXECUTABLES "Creation Engine" "Creation Engine")
set(CPACK_CREATE_DESKTOP_LINKS "Creation Engine")

include(CPack)
