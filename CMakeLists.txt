cmake_minimum_required(VERSION 3.22)

set(CREATION_ENGINE_VERSION "0.0.1" CACHE STRING "Creation Engine product version")
project(CreationEngine VERSION ${CREATION_ENGINE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to your JUCE root directory before configuring (same JUCE checkout Creation Station uses).")
endif()

enable_testing()

# --- GS: CEL scripting language (LALR parser + LLVM JIT) -----------------
# LLVM comes from a vcpkg-managed install (see vcpkg.json's pinned
# builtin-baseline), not the full vcpkg CMake toolchain-file integration
# -- that would require CMAKE_TOOLCHAIN_FILE to be set on this build
# directory's very first configure, which AGENTS.md says not to do
# without asking first. `vcpkg install` run from the repo root (manifest
# mode, per vcpkg.json) lands packages in vcpkg_installed/<triplet>/ next
# to the manifest by default -- pointing CMAKE_PREFIX_PATH there is
# additive and safe to introduce on any reconfigure of the existing
# build/ directory, and needs no knowledge of where the vcpkg tool
# itself is checked out.
option(CE_ENABLE_SCRIPTING "Build the CEL scripting language module (Language/, requires vcpkg LLVM)" ON)
if(CE_ENABLE_SCRIPTING)
    set(CE_VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows")
    if(NOT EXISTS "${CE_VCPKG_INSTALLED_DIR}")
        set(CE_VCPKG_INSTALLED_DIR "D:/000 Creation Engine/vcpkg_installed/x64-windows")
    endif()
    if(NOT EXISTS "${CE_VCPKG_INSTALLED_DIR}")
        message(FATAL_ERROR "CE_ENABLE_SCRIPTING is ON but ${CE_VCPKG_INSTALLED_DIR} doesn't exist. Run `vcpkg install --triplet x64-windows` from the repo root first (see README.md's \"Scripting language build\" section), or configure with -DCE_ENABLE_SCRIPTING=OFF to skip the scripting module.")
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${CE_VCPKG_INSTALLED_DIR}")

    # Suite-level CEL: shared/NodeSystem and shared/CEL are the authoritative
    # source for the frontend/JIT/nodegen (creation_suite_cel_frontend/jit/nodegen,
    # aliased to ce_lang_frontend/ce_lang_jit/ce_lang_nodegen) -- added here,
    # before Language/, since Language/CMakeLists.txt calls
    # target_include_directories() on these targets directly, which requires
    # them to already exist at that point (unlike target_link_libraries,
    # which CMake resolves lazily against the full target graph).
    if(NOT TARGET node_system)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/NodeSystem" CreationSuiteSharedNodeSystem)
    endif()
    if(NOT TARGET ce_lang_frontend)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/CEL" CreationSuiteSharedCel)
    endif()

    add_subdirectory(Language)
endif()

add_subdirectory("${JUCE_DIR}" juce)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt/CMakeLists.txt")
    add_subdirectory(third_party/entt)
elseif(EXISTS "D:/000 Creation Engine/third_party/entt/CMakeLists.txt")
    add_subdirectory("D:/000 Creation Engine/third_party/entt" EnttExternal)
else()
    message(FATAL_ERROR "Could not find the entt dependency for Creation Engine.")
endif()

# --- Shared, framework-agnostic libraries --------------------------------
# NodeSystem now comes from shared/NodeSystem (added above, before
# Language/, when CE_ENABLE_SCRIPTING is on). If scripting is disabled,
# fall back to Engine's own local copy so NodeSystem-dependent code
# (HierarchyPanel, node editor UI) still has a target to link.
if(NOT TARGET node_system)
    add_subdirectory(NodeSystem)
endif()
add_subdirectory(EngineCore)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/VFS" CreationSuiteSharedVFS)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/AssetSystem" CreationSuiteSharedAssetSystem)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Services" CreationSuiteSharedServices)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI" CreationSuiteSharedUI)

# --- Shared, juce_core-only libraries -------------------------------------
# Not framework-agnostic like the two above (needs juce::juce_core for
# juce::ZipFile), but still no GUI/rendering dependency, so a future
# server-side asset loader could link this without pulling in anything
# beyond what CreationEngineServer already links.

# ce_lang_jit/ce_engine_lang_world/ce_lang_nodegen only exist when
# CE_ENABLE_SCRIPTING is on (they come from shared/CEL + Language/, both
# skipped above otherwise) -- linking them unconditionally fails at link
# time (not configure time, since target_link_libraries doesn't validate
# target existence until generate) with a plain "cannot open input file"
# once scripting is disabled.
set(CE_SCRIPTING_LINK_LIBS "")
if(CE_ENABLE_SCRIPTING)
    set(CE_SCRIPTING_LINK_LIBS ce_lang_jit ce_engine_lang_world ce_lang_nodegen)
endif()

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
# directory (D:\CreationSuite-Workspaces\claude-<config>-bin) so it's
# reachable at a stable path without needing OS symlink privileges.
# Every suite agent does this for its own repo -- see AGENTS.md.
add_custom_command(TARGET CreationEngineEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationEngineEditor>" "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMENT "Copying CreationEngineEditor to the shared claude bin directory"
)

juce_generate_juce_header(CreationEngineEditor)

target_sources(CreationEngineEditor PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
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
    Source/Render/Import/ObjLoader.h
    Source/Render/Import/ObjLoader.cpp
    Source/Render/Scene/Light.h
    Source/Views/LightPanel.h
    Source/Views/LightPanel.cpp
    Source/Views/TransportLookAndFeel.h
    Source/Views/TransportLookAndFeel.cpp
    Source/Views/TransportBar.h
    Source/Views/TransportBar.cpp
    Source/Views/ViewModeBar.h
    Source/Views/ViewModeBar.cpp
    Source/Views/PlaceholderPanel.h
    Source/Views/PlaceholderPanel.cpp
    Source/Views/HierarchyPanel.h
    Source/Views/HierarchyPanel.cpp
    Source/Views/TransformPanel.h
    Source/Views/TransformPanel.cpp
    Source/Views/MaterialsPanel.h
    Source/Views/MaterialsPanel.cpp
    Source/Views/ScriptPanel.h
    Source/Views/ScriptPanel.cpp
    Source/Views/CelCodeTokeniser.h
    Source/Views/CelCodeTokeniser.cpp
    Source/Views/ImportPanel.h
    Source/Views/ImportPanel.cpp
    Source/Views/NodeEditor/NodeGraphComponent.h
    Source/Views/NodeEditor/NodeGraphComponent.cpp
    Source/Views/NodeEditor/NodePalette.h
    Source/Views/NodeEditor/NodePalette.cpp
    Source/Views/NodeEditor/NodeInspector.h
    Source/Views/NodeEditor/NodeInspector.cpp
    Source/Views/NodeEditor/GeneratedCodeView.h
    Source/Views/NodeEditor/GeneratedCodeView.cpp
    Source/Views/NodeEditor/LogicPanel.h
    Source/Views/NodeEditor/LogicPanel.cpp
    Source/Scene/Components.h
    Source/Scene/EngineSceneSerializer.h
    Source/Scene/EngineSceneSerializer.cpp
    Source/Scene/AssetCatalog.h
    Source/Scene/AssetCatalog.cpp
    Source/Scene/AssetPlacement.h
    Source/Scene/AssetPlacement.cpp
    Source/Scene/ScriptCatalog.h
    Source/Scene/ScriptCatalog.cpp
    Source/Scene/AnimationSampler.h
    Source/Scene/AnimationSampler.cpp
    Source/Scene/AnimationSlicer.h
    Source/Scene/AnimationSlicer.cpp
    Source/Import/AssetImporter.h
    Source/Import/ImporterRegistry.h
    Source/Import/ImporterRegistry.cpp
    Source/Import/Importers/GltfAssetImporter.h
    Source/Import/Importers/GltfAssetImporter.cpp
    Source/Import/Importers/ObjAssetImporter.h
    Source/Import/Importers/ObjAssetImporter.cpp
    Source/Import/Importers/AudioAssetImporter.h
    Source/Import/Importers/AudioAssetImporter.cpp
    Source/Import/Importers/TextureAssetImporter.h
    Source/Import/Importers/TextureAssetImporter.cpp
    Source/Import/Importers/ScriptAssetImporter.h
    Source/Import/Importers/ScriptAssetImporter.cpp
    Source/Audio/AudioCatalog.h
    Source/Audio/AudioCatalog.cpp
    Source/Audio/AudioPreviewPlayer.h
    Source/Audio/AudioPreviewPlayer.cpp
)

target_include_directories(CreationEngineEditor PRIVATE Source third_party)

target_compile_definitions(CreationEngineEditor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_OPENGL3=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationEngineEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationEngineEditor,JUCE_VERSION>"
    CE_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Source/Render/Shaders"
    CE_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    CE_ENABLE_SCRIPTING=$<BOOL:${CE_ENABLE_SCRIPTING}>
)

target_link_libraries(CreationEngineEditor PRIVATE
    engine_core
    node_system
    creation_suite_asset_system
    creation_suite_ui
    creation_suite_ui_assets
    ${CE_SCRIPTING_LINK_LIBS}
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# --- Dedicated server executable ------------------------------------------
# Headless build: no rendering, no client-only assets (spec section 6.2).
# Only juce_core/juce_events for cross-platform utilities (threads, time,
# argument parsing, logging) — never juce_gui_* or juce_opengl. GS6 adds
# ce_lang_jit (CEL script compilation/execution via Simulation::Step) —
# that's an LLVM dependency, not a GUI one, so it doesn't violate the
# "headless" property above; Language's own header firewall (see
# Language/include/lang/jit/runtime.h) keeps llvm/*.h out of every
# translation unit outside Language/src/jit/ regardless.
juce_add_console_app(CreationEngineServer
    PRODUCT_NAME "Creation Engine Server"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine.server"
)

# Mirrors the built executable into the workspace-level shared bin
# directory (D:\CreationSuite-Workspaces\claude-<config>-bin) so it's
# reachable at a stable path without needing OS symlink privileges.
# Every suite agent does this for its own repo -- see AGENTS.md.
add_custom_command(TARGET CreationEngineServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationEngineServer>" "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMENT "Copying CreationEngineServer to the shared claude bin directory"
)

juce_generate_juce_header(CreationEngineServer)

target_sources(CreationEngineServer PRIVATE
    Server/Source/Main.cpp
)

target_compile_definitions(CreationEngineServer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationEngineServer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationEngineServer,JUCE_VERSION>"
)

target_link_libraries(CreationEngineServer PRIVATE
    engine_core
    node_system
    ce_lang_jit
    ce_engine_lang_world
    juce::juce_core
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# GS6: the concrete proof of "the same rule module runs identically
# client-side or server-side" -- runs the SAME script through celc
# --run-simulation (Simulation::Step driven directly, no networking) and
# CreationEngineServer --script/--ticks/--dt (its own headless batch
# mode, see Server/Source/Main.cpp), and asserts their two-line stdout
# is byte-for-byte identical. Lives here, not in Language/CMakeLists.txt,
# since it's the one test that needs both executables and both are only
# fully defined once the whole top-level configure has run.
# celc only exists when CE_ENABLE_SCRIPTING is on (see the vcpkg-LLVM
# gate near the top of this file) -- guard this test's registration so
# a scripting-disabled configure (e.g. release CI, which never triggers
# an LLVM build) doesn't fail at generate time referencing a target
# that was never defined.
if(TARGET celc AND TARGET CreationEngineServer)
    add_test(
        NAME gs6_simulation_server_parity
        COMMAND ${CMAKE_COMMAND}
                -DCELC_EXE=$<TARGET_FILE:celc>
                -DSERVER_EXE=$<TARGET_FILE:CreationEngineServer>
                -DTEST_CEL=${CMAKE_CURRENT_SOURCE_DIR}/Language/tests/simulation/orbit.cel
                -DTICKS=300
                -DDT=0.016666667
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Language/tests/run_simulation_parity_test.cmake"
    )
endif()

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
