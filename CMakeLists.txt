cmake_minimum_required(VERSION 3.22)

set(CREATION_ENGINE_VERSION "0.0.1" CACHE STRING "Djehuti Engine product version")
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
        "Djehuti Engine requires the OpenXR SDK and did not find it. "
        "Expected it at ${CREATION_OPENXR_SHARED_DEPS_ROOT} (shared across "
        "every suite agent workspace). If it has moved, reconfigure with "
        "-DCREATION_OPENXR_INCLUDE_DIR=<path>/include "
        "-DCREATION_OPENXR_LOADER_LIBRARY=<path>/lib/openxr_loader.lib.")
endif()
set(CREATION_ENGINE_HAS_OPENXR 1)
message(STATUS "Djehuti Engine: OpenXR runtime support enabled (${CREATION_OPENXR_INCLUDE_DIR})")

# LLVM / vcpkg Build Rule (see AGENTS.md): LLVM must NEVER be managed by
# this app's own vcpkg.json manifest. A real incident happened here --
# `vcpkg install` (run to add an unrelated small dependency) reconciled the
# WHOLE manifest, which included llvm at the time, and both removed the
# already-built LLVM AND started rebuilding it from source before it was
# caught and killed. `llvm` has been deliberately removed from vcpkg.json
# entirely as the actual fix (not just a warning) -- with it gone from the
# manifest, `vcpkg install` on this project can structurally never touch
# LLVM again, no matter why someone runs it.
#
# This means LLVM discovery can no longer rely on vcpkg's own manifest
# machinery at all; the path below used to be found only because an old
# cached LLVM_DIR value happened to still point at it from before llvm was
# removed here -- that was invisible and undocumented. This makes the same
# path explicit, and fails loudly and immediately (not with vcpkg's own
# confusing "could not find LLVMConfig.cmake" error two subdirectories
# deep in third_party/FrustLang) if LLVM isn't actually there, with a
# message that says exactly what NOT to do about it.
set(CREATION_ENGINE_LLVM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/share/llvm"
    CACHE PATH "LLVM CMake package directory -- manually provisioned, NEVER vcpkg-managed for this app.")
if (NOT EXISTS "${CREATION_ENGINE_LLVM_DIR}/LLVMConfig.cmake")
    message(FATAL_ERROR
        "LLVM is missing at ${CREATION_ENGINE_LLVM_DIR} (LLVMConfig.cmake not "
        "found). This project deliberately does NOT vcpkg-manage LLVM -- see "
        "the LLVM / vcpkg Build Rule above and in AGENTS.md. "
        "DO NOT run 'vcpkg install' to fix this -- it no longer knows about "
        "llvm at all and cannot help. LLVM needs to be restored/rebuilt "
        "through some other deliberate, explicitly-approved process, then "
        "placed back at this exact path (or point -DCREATION_ENGINE_LLVM_DIR= "
        "at wherever it actually lives).")
endif()
set(LLVM_DIR "${CREATION_ENGINE_LLVM_DIR}" CACHE PATH "" FORCE)

# Djehuti Bridge engine-side handoff (DjehutiImportWatcher): a WebSocket
# CLIENT connection to services/VfsService's existing entryChanged
# broadcast. Deliberately NOT added to this app's own vcpkg.json --
# `vcpkg install` reconciles the WHOLE manifest (including `llvm` above),
# which triggered a real, wasteful full LLVM rebuild the one time this was
# tried (see AGENTS.md's "LLVM / vcpkg Build Rule": never run vcpkg
# install here without an explicit yes). Instead, this reuses
# services/VfsService's OWN already-built ixwebsocket (and its zlib
# dependency, needed for permessage-deflate) directly -- same manual
# find_path/find_library pattern already used for openxr-loader above,
# just pointed at a sibling project's vcpkg_installed tree instead of an
# external SDK location. Build services/VfsService at least once first so
# these exist.
set(CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../services/VfsService/build/vcpkg_installed/x64-windows")
find_path(CREATION_ENGINE_IXWEBSOCKET_INCLUDE_DIR NAMES ixwebsocket/IXWebSocket.h
    HINTS "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}/include")
find_library(CREATION_ENGINE_IXWEBSOCKET_LIBRARY_RELEASE NAMES ixwebsocket
    HINTS "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}/lib")
find_library(CREATION_ENGINE_IXWEBSOCKET_LIBRARY_DEBUG NAMES ixwebsocket
    HINTS "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}/debug/lib")
find_library(CREATION_ENGINE_ZLIB_LIBRARY_RELEASE NAMES z
    HINTS "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}/lib")
find_library(CREATION_ENGINE_ZLIB_LIBRARY_DEBUG NAMES zd
    HINTS "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}/debug/lib")

if (NOT CREATION_ENGINE_IXWEBSOCKET_INCLUDE_DIR OR NOT CREATION_ENGINE_IXWEBSOCKET_LIBRARY_RELEASE OR NOT CREATION_ENGINE_IXWEBSOCKET_LIBRARY_DEBUG)
    message(FATAL_ERROR
        "Djehuti Engine requires ixwebsocket (reused from services/VfsService's "
        "own vcpkg_installed tree, not vendored separately here) for the "
        "Djehuti Bridge import watcher, and did not find it at "
        "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}. Build services/VfsService "
        "at least once first (its own CMake configure installs this).")
endif()

# Jolt Physics: same LLVM/vcpkg-manifest-safety reasoning as ixwebsocket above
# -- NEVER added to this app's own vcpkg.json, since `vcpkg install` there
# reconciles the whole manifest and would touch the LLVM install root. Jolt
# is vendored into a completely separate, isolated vcpkg install
# (third_party/jolt-vcpkg/vcpkg_installed), installed via classic mode
# (`vcpkg install joltphysics:x64-windows --classic --x-install-root=...`),
# never touching any manifest at all. Unlike ixwebsocket/OpenXR above, this
# uses find_package (not manual find_path/find_library): Jolt's own
# vcpkg-generated Jolt::Jolt imported target carries required
# INTERFACE_COMPILE_DEFINITIONS (JPH_USE_CPU_COMPUTE, JPH_OBJECT_STREAM,
# JPH_FLOATING_POINT_EXCEPTIONS_ENABLED, etc.) that MUST match between the
# prebuilt static library and every consuming translation unit -- hand-
# duplicating those via manual find_library would risk a silent ABI/struct-
# layout mismatch, exactly the class of bug Jolt's own build docs warn
# about.
set(CREATION_ENGINE_JOLT_VCPKG_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/jolt-vcpkg/vcpkg_installed/x64-windows")
if (NOT EXISTS "${CREATION_ENGINE_JOLT_VCPKG_ROOT}/share/Jolt/JoltConfig.cmake")
    message(FATAL_ERROR
        "Djehuti Engine requires Jolt Physics and did not find it at "
        "${CREATION_ENGINE_JOLT_VCPKG_ROOT}. Install it into that isolated "
        "root with: D:/vcpkg/vcpkg.exe install joltphysics:x64-windows "
        "--classic --x-install-root=\"${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/jolt-vcpkg/vcpkg_installed\" "
        "-- do NOT add joltphysics to this app's own vcpkg.json (see the "
        "LLVM / vcpkg Build Rule above).")
endif()
list(APPEND CMAKE_PREFIX_PATH "${CREATION_ENGINE_JOLT_VCPKG_ROOT}")
find_package(Jolt CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Several Engine smoke targets compile generated JUCE and FRust-adjacent
# sources in parallel even when the outer MSBuild invocation uses /m:1.
# MSVC needs /FS to synchronize their shared debug-symbol database.
if(MSVC)
    add_compile_options(/FS)
endif()

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
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/CharacterSystem" CreationSuiteSharedCharacterSystem)
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
    PRODUCT_NAME "Djehuti Engine"
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
    Source/Views/EditorAvatarPanel.h
    Source/Views/EditorAvatarPanel.cpp
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
    Source/Input/InputBindingTypes.h
    Source/Input/InputBindingDocumentStore.h
    Source/Input/InputBindingDocumentStore.cpp
    Source/Input/InputActionSystem.h
    Source/Input/InputActionSystem.cpp
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
    Source/Import/DjehutiImportWatcher.h
    Source/Import/DjehutiImportWatcher.cpp
    Source/Physics/PhysicsComponents.h
    Source/Physics/PhysicsWorld.h
    Source/Physics/PhysicsWorld.cpp
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
    Source/Views/InputBindingsPanel.h
    Source/Views/InputBindingsPanel.cpp
    Source/Runtime/GameClientWindow.h
    Source/Runtime/GameClientWindow.cpp
    Source/Runtime/CameraDirector.h
    Source/Runtime/CameraDirector.cpp
    Source/Runtime/PuppetRuntime.h
    Source/Runtime/PossessionService.h
    Source/Runtime/PossessionService.cpp
    Source/Runtime/RuntimeWorldRunner.h
    Source/Runtime/RuntimeWorldRunner.cpp
    Source/Character/CharacterDefinition.h
    Source/Character/CharacterDefinition.cpp
    Source/Character/PossessedCharacter.h
    Source/Character/PossessedCharacter.cpp
    Source/Views/PlaceholderPanel.h
    Source/Views/PlaceholderPanel.cpp
    Source/Views/TransformPanel.h
    Source/Views/TransformPanel.cpp
    Source/Views/MaterialsPanel.h
    Source/Views/MaterialsPanel.cpp
    Source/Views/PhysicsPropertiesPanel.h
    Source/Views/PhysicsPropertiesPanel.cpp
    Source/Views/PropertiesPanel.h
    Source/Views/PropertiesPanel.cpp
    Source/Views/MaterialGraphPanel.h
    Source/Views/MaterialGraphPanel.cpp
    Source/Views/ImportPanel.h
    Source/Views/ImportPanel.cpp
    Source/Views/ContentBrowserPanel.h
    Source/Views/ContentBrowserPanel.cpp
    Source/Views/SceneBuiltinsPanel.h
    Source/Views/SceneBuiltinsPanel.cpp
    Source/Views/SceneGraphPanel.h
    Source/Views/SceneGraphPanel.cpp
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
    Source/Scene/AnimatorControl.h
    Source/Scene/AnimatorControl.cpp
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
    ${CREATION_ENGINE_IXWEBSOCKET_INCLUDE_DIR}
)

if (CREATION_ENGINE_HAS_OPENXR)
    target_include_directories(CreationEngineEditor PRIVATE ${CREATION_OPENXR_INCLUDE_DIR})
    target_link_libraries(CreationEngineEditor PRIVATE ${CREATION_OPENXR_LOADER_LIBRARY})
endif()

target_compile_definitions(CreationEngineEditor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_OPENGL3=1
    # ixwebsocket's own zlib (permessage-deflate) would otherwise collide
    # with JUCE's bundled zlib symbols at link time -- same real conflict
    # services/VfsService's own CMakeLists.txt already documents and works
    # around identically.
    JUCE_INCLUDE_ZLIB_CODE=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationEngineEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationEngineEditor,JUCE_VERSION>"
    CE_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Source/Render/Shaders"
    CE_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    CE_HAS_OPENXR=${CREATION_ENGINE_HAS_OPENXR}
)

target_link_libraries(CreationEngineEditor PRIVATE
    Jolt::Jolt
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
    $<$<CONFIG:Debug>:${CREATION_ENGINE_IXWEBSOCKET_LIBRARY_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${CREATION_ENGINE_IXWEBSOCKET_LIBRARY_RELEASE}>
    $<$<CONFIG:Debug>:${CREATION_ENGINE_ZLIB_LIBRARY_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${CREATION_ENGINE_ZLIB_LIBRARY_RELEASE}>
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
    Source/Input/InputActionSystem.cpp
    Source/Input/InputBindingDocumentStore.cpp
    Source/Physics/PhysicsWorld.cpp
    Source/Scene/AnimatorControl.cpp
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
    Jolt::Jolt
    engine_core
    node_system
    creation_suite_asset_system
    creation_suite_frust_plugin_runtime
    juce::juce_core
    juce::juce_gui_basics
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
    Source/Input/InputActionSystem.cpp
    Source/Input/InputBindingDocumentStore.cpp
    Source/Physics/PhysicsWorld.cpp
    Source/Scene/AnimatorControl.cpp
)
target_include_directories(CreationEngineObjectBehaviorSmoke PRIVATE Source)
target_link_libraries(CreationEngineObjectBehaviorSmoke PRIVATE
    Jolt::Jolt
    engine_core
    node_system
    creation_suite_asset_system
    creation_suite_frust_plugin_runtime
    juce::juce_core
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_opengl
)
target_compile_definitions(CreationEngineObjectBehaviorSmoke PRIVATE
    CE_ENGINE_OBJECT_BEHAVIOR_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/SetCurrentObjectX.frust"
)
add_test(NAME creation_engine_object_behavior_smoke COMMAND CreationEngineObjectBehaviorSmoke)

add_executable(CreationEngineInputActionSystemSmoke
    tests/InputActionSystemSmoke.cpp
    Source/Input/InputActionSystem.cpp
    Source/Input/InputBindingDocumentStore.cpp
    Source/Assets/AssetPackStore.cpp
)
target_include_directories(CreationEngineInputActionSystemSmoke PRIVATE Source)
target_link_libraries(CreationEngineInputActionSystemSmoke PRIVATE
    engine_core
    node_system
    creation_suite_asset_system
    creation_suite_services
    juce::juce_core
    juce::juce_gui_basics
)
add_test(NAME creation_engine_input_action_system_smoke COMMAND CreationEngineInputActionSystemSmoke)

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

# Phase 5 of the Jolt vendoring plan -- proves RegisterCorePhysicsNodes'
# Domain::Core physics nodes actually compile through frust_codegen and run,
# not just that the registration code compiles.
add_executable(CreationEnginePhysicsNodeCodegenSmoke
    tests/PhysicsNodeCodegenSmoke.cpp
)
target_link_libraries(CreationEnginePhysicsNodeCodegenSmoke PRIVATE node_system)
target_link_libraries(CreationEnginePhysicsNodeCodegenSmoke PRIVATE creation_suite_frust_plugin_runtime)
add_test(NAME creation_engine_physics_node_codegen_smoke COMMAND CreationEnginePhysicsNodeCodegenSmoke)

add_executable(CreationEngineFrustGenericNodeReflectionSmoke
    tests/FrustGenericNodeReflectionSmoke.cpp
)
target_link_libraries(CreationEngineFrustGenericNodeReflectionSmoke PRIVATE creation_suite_frust_plugin_runtime)
target_compile_definitions(CreationEngineFrustGenericNodeReflectionSmoke PRIVATE
    CE_GENERIC_NODE_REFLECTION_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/GenericNodeReflection.frust"
)
add_test(NAME creation_engine_frust_generic_node_reflection_smoke COMMAND CreationEngineFrustGenericNodeReflectionSmoke)

add_executable(CreationEngineGenericNodeCodegenSmoke
    tests/GenericNodeCodegenSmoke.cpp
)
target_link_libraries(CreationEngineGenericNodeCodegenSmoke PRIVATE node_system)
target_link_libraries(CreationEngineGenericNodeCodegenSmoke PRIVATE creation_suite_frust_plugin_runtime)
target_compile_definitions(CreationEngineGenericNodeCodegenSmoke PRIVATE
    CE_CORE_NODE_LIBRARY="${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodesLibrary.frust"
)
add_test(NAME creation_engine_generic_node_codegen_smoke COMMAND CreationEngineGenericNodeCodegenSmoke)

add_executable(CreationEngineF64FfiCheckSmoke
    tests/F64FfiCheckSmoke.cpp
)
target_link_libraries(CreationEngineF64FfiCheckSmoke PRIVATE creation_suite_frust_plugin_runtime)
target_compile_definitions(CreationEngineF64FfiCheckSmoke PRIVATE
    CE_F64_FFI_CHECK_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/F64FfiCheck.frust"
)
add_test(NAME creation_engine_f64_ffi_check_smoke COMMAND CreationEngineF64FfiCheckSmoke)

# Phase 1 of the Jolt vendoring plan -- prove Jolt links and runs at all,
# isolated from any engine wrapper code. See CREATION_ENGINE_JOLT_VCPKG_ROOT
# above.
add_executable(CreationEnginePhysicsJoltLinkSmoke
    tests/PhysicsJoltLinkSmoke.cpp
)
target_link_libraries(CreationEnginePhysicsJoltLinkSmoke PRIVATE Jolt::Jolt)
add_test(NAME creation_engine_physics_jolt_link_smoke COMMAND CreationEnginePhysicsJoltLinkSmoke)

# Phase 5 of the Jolt vendoring plan -- the real end-to-end proof: a
# dynamic sphere configured purely via RigidBodyComponent/ColliderComponent
# (the same components a Schematic config node sets) falls, collides with
# a static ground body, fires a collision event, and settles at the
# expected height.
add_executable(CreationEnginePhysicsWorldSmoke
    tests/PhysicsWorldSmoke.cpp
    Source/Physics/PhysicsWorld.cpp
)
target_include_directories(CreationEnginePhysicsWorldSmoke PRIVATE Source)
target_link_libraries(CreationEnginePhysicsWorldSmoke PRIVATE Jolt::Jolt engine_core)
add_test(NAME creation_engine_physics_world_smoke COMMAND CreationEnginePhysicsWorldSmoke)

# Possessable Designer Character plan, Phase 5 -- proves CharacterVirtual
# sensor-vs-solid collider behavior (button pass-through with a contact
# event, vs. a normal wall actually blocking movement).
add_executable(CreationEngineCharacterControllerSmoke
    tests/CharacterControllerSmoke.cpp
    Source/Physics/PhysicsWorld.cpp
)
target_include_directories(CreationEngineCharacterControllerSmoke PRIVATE Source)
target_link_libraries(CreationEngineCharacterControllerSmoke PRIVATE Jolt::Jolt engine_core)
add_test(NAME creation_engine_character_controller_smoke COMMAND CreationEngineCharacterControllerSmoke)

# DefaultScene is authored starter content, not a runtime auto-collider
# exception. Keep its visible geometry and explicit static collision setup in
# lockstep so a newly created Game always gives a possessed character ground.
add_executable(CreationEngineDefaultScenePhysicsSmoke
    tests/DefaultScenePhysicsSmoke.cpp
)
target_compile_definitions(CreationEngineDefaultScenePhysicsSmoke PRIVATE
    CE_DEFAULT_SCENE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/EnginePack/scenes/DefaultScene.xml"
)
add_test(NAME creation_engine_default_scene_physics_smoke COMMAND CreationEngineDefaultScenePhysicsSmoke)

# --- Dedicated server executable ------------------------------------------
# Headless build: no rendering or client-only assets. Only juce_core and
# juce_events provide cross-platform utilities for the server shell.
juce_add_console_app(CreationEngineServer
    PRODUCT_NAME "Djehuti Engine Server"
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

set(CPACK_PACKAGE_NAME "Djehuti Engine")
set(CPACK_PACKAGE_VENDOR "LagDaemon Software")
set(CPACK_PACKAGE_CONTACT "LagDaemon Software")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Djehuti Engine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "creation-engine-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Djehuti Engine")
set(CPACK_GENERATOR "ZIP;WIX")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Djehuti Engine")
set(CPACK_PACKAGE_EXECUTABLES "Djehuti Engine" "Djehuti Engine")
set(CPACK_CREATE_DESKTOP_LINKS "Djehuti Engine")

include(CPack)
