cmake_minimum_required(VERSION 3.22)

set(CREATION_ENGINE_VERSION "0.0.1" CACHE STRING "Djehuti Engine product version")
project(CreationEngine VERSION ${CREATION_ENGINE_VERSION} LANGUAGES C CXX)

get_filename_component(CREATION_ENGINE_SUITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CREATION_OPENXR_SHARED_DEPS_ROOT "${CREATION_ENGINE_SUITE_DIR}/../openxr-deps/x64-windows"
    CACHE PATH "Prebuilt OpenXR SDK root")
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
        "Expected it at ${CREATION_OPENXR_SHARED_DEPS_ROOT}. If it has moved, reconfigure with "
        "-DCREATION_OPENXR_SHARED_DEPS_ROOT=<sdk-root> or set "
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

# The Bridge watcher uses VfsService's WebSocket client dependency. Both
# libraries can also be supplied as existing standalone package trees.
set(CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT
    "${CREATION_ENGINE_SUITE_DIR}/services/VfsService/build/vcpkg_installed/x64-windows"
    CACHE PATH "Prebuilt ixwebsocket package root")
set(CREATION_ENGINE_ZLIB_VCPKG_ROOT "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}"
    CACHE PATH "Prebuilt zlib package root")
set(ZLIB_ROOT "${CREATION_ENGINE_ZLIB_VCPKG_ROOT}")
find_library(ZLIB_LIBRARY_RELEASE NAMES z zlib zlibstatic
    PATHS "${ZLIB_ROOT}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY_DEBUG NAMES zd zlibd zlibstaticd
    PATHS "${ZLIB_ROOT}/debug/lib" NO_DEFAULT_PATH)
find_package(ZLIB MODULE REQUIRED)
if (NOT ZLIB_LIBRARY_DEBUG OR NOT ZLIB_LIBRARY_RELEASE)
    message(FATAL_ERROR
        "Djehuti Engine requires the zlib Debug and Release libraries. "
        "Set -DCREATION_ENGINE_ZLIB_VCPKG_ROOT to the prebuilt package root.")
endif()
find_package(ixwebsocket CONFIG REQUIRED
    PATHS "${CREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT}" NO_DEFAULT_PATH)
set_target_properties(ixwebsocket::ixwebsocket PROPERTIES
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
)

# Jolt's exported target provides the compile definitions needed for the
# prebuilt library's ABI.
set(Jolt_DIR "${CREATION_ENGINE_SUITE_DIR}/third_party/jolt-vcpkg/vcpkg_installed/x64-windows/share/Jolt"
    CACHE PATH "Directory containing the prebuilt JoltConfig.cmake")
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
find_package(Threads REQUIRED)
add_subdirectory(EngineCore)

include(FetchContent)
cmake_policy(PUSH)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(engine_httplib
    URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/refs/tags/v0.67.0
    URL_HASH SHA256=be711ddaf7a3655d8f23aa5655709250b24d247aedd4b2629989d18b6164e5be
    SOURCE_SUBDIR engine-header-only
)
FetchContent_MakeAvailable(engine_httplib)
cmake_policy(POP)
add_library(engine_developer_api STATIC
    Source/DeveloperApi/DeveloperApi.cpp
    Source/DeveloperApi/HttpTransport.cpp
    Source/DeveloperApi/PersistenceCommands.cpp
    Source/DeveloperApi/RuntimeCommands.cpp
)
target_include_directories(engine_developer_api PUBLIC Source)
target_include_directories(engine_developer_api SYSTEM PRIVATE "${engine_httplib_SOURCE_DIR}" "${JUCE_DIR}/modules")
target_compile_features(engine_developer_api PUBLIC cxx_std_20)
target_compile_definitions(engine_developer_api PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_USE_CURL=0 JUCE_INCLUDE_ZLIB_CODE=0
    $<$<CONFIG:Debug>:DEBUG=1> $<$<CONFIG:Debug>:_DEBUG=1>)
target_link_libraries(engine_developer_api
    PUBLIC engine_core
    PRIVATE engine_sqlite_persistence engine_rocksdb_persistence Threads::Threads)
if(WIN32)
    target_link_libraries(engine_developer_api PRIVATE ws2_32)
endif()



add_executable(CreationEnginePersistenceSmoke tests/PersistenceSmoke.cpp)
target_link_libraries(CreationEnginePersistenceSmoke PRIVATE engine_sqlite_persistence engine_rocksdb_persistence Threads::Threads)
add_test(NAME creation_engine_persistence_smoke COMMAND CreationEnginePersistenceSmoke)
add_test(NAME creation_engine_sqlite_conformance
    COMMAND CreationEnginePersistenceSmoke --sqlite "${CMAKE_CURRENT_BINARY_DIR}/Testing")

add_executable(CreationEngineSQLiteRecovery tests/SQLitePersistenceRecovery.cpp)
target_link_libraries(CreationEngineSQLiteRecovery PRIVATE engine_sqlite_persistence djehuti_sqlite Threads::Threads)
add_test(NAME creation_engine_sqlite_recovery
    COMMAND ${CMAKE_COMMAND}
        "-DTEST_EXECUTABLE=$<TARGET_FILE:CreationEngineSQLiteRecovery>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/SQLitePersistenceRecovery.cmake")
set_tests_properties(creation_engine_sqlite_conformance creation_engine_sqlite_recovery PROPERTIES TIMEOUT 90)

add_test(NAME creation_engine_rocksdb_conformance
    COMMAND CreationEnginePersistenceSmoke --rocksdb "${CMAKE_CURRENT_BINARY_DIR}/Testing")

add_executable(CreationEngineRocksDBRecovery tests/RocksDBPersistenceRecovery.cpp)
target_link_libraries(CreationEngineRocksDBRecovery PRIVATE engine_rocksdb_persistence engine_sqlite_persistence Threads::Threads)
add_test(NAME creation_engine_rocksdb_recovery
    COMMAND ${CMAKE_COMMAND}
        "-DTEST_EXECUTABLE=$<TARGET_FILE:CreationEngineRocksDBRecovery>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RocksDBPersistenceRecovery.cmake")
set_tests_properties(creation_engine_rocksdb_conformance creation_engine_rocksdb_recovery PROPERTIES TIMEOUT 180)

add_executable(CreationEnginePortablePersistenceQuery tests/PortablePersistenceQuery.cpp)
target_link_libraries(CreationEnginePortablePersistenceQuery PRIVATE
    engine_rocksdb_persistence engine_sqlite_persistence Threads::Threads)
add_test(NAME creation_engine_portable_persistence_query
    COMMAND ${CMAKE_COMMAND}
        "-DTEST_EXECUTABLE=$<TARGET_FILE:CreationEnginePortablePersistenceQuery>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/PortablePersistenceQuery.cmake")
set_tests_properties(creation_engine_portable_persistence_query PROPERTIES TIMEOUT 180)

add_executable(CreationEnginePersistenceBenchmark tests/PersistenceBenchmark.cpp)
target_link_libraries(CreationEnginePersistenceBenchmark PRIVATE
    engine_rocksdb_persistence engine_sqlite_persistence djehuti_sqlite Threads::Threads)
if(WIN32)
    target_link_libraries(CreationEnginePersistenceBenchmark PRIVATE Psapi)
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/VFS" CreationSuiteSharedVFS)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/AssetSystem" CreationSuiteSharedAssetSystem)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/CharacterSystem" CreationSuiteSharedCharacterSystem)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Services" CreationSuiteSharedServices)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI" CreationSuiteSharedUI)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/FrustPluginRuntime" CreationSuiteSharedFrustPluginRuntime)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/NodeLibraryLoader" CreationSuiteSharedNodeLibraryLoader)

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
    PRODUCT_NAME "DjehutiEngine"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/icon.png"
)

# Keep the bundled plugins and assets beside the editor executable.
add_custom_command(TARGET CreationEngineEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/EngineLifecycle.frust" "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodes.frust" "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodesLibrary.frust" "$<TARGET_FILE_DIR:CreationEngineEditor>/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/EnginePack" "$<TARGET_FILE_DIR:CreationEngineEditor>/EnginePack"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/models" "$<TARGET_FILE_DIR:CreationEngineEditor>/EnginePack/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/packages" "$<TARGET_FILE_DIR:CreationEngineEditor>/EnginePack/packages"
)

if(WIN32)
    # Prebuilt import libraries can have their DLLs in the sibling bin directory.
    function(creation_engine_stage_package_dlls target library config)
        get_filename_component(library_dir "${library}" DIRECTORY)
        get_filename_component(library_name "${library}" NAME_WE)
        foreach(dll_name IN ITEMS "${library_name}.dll" ${ARGN})
            set(dll "${library_dir}/../bin/${dll_name}")
            if(EXISTS "${dll}")
                set(copy_command "${CMAKE_COMMAND}" -E copy_if_different
                    "${dll}" "$<TARGET_FILE_DIR:${target}>")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND "$<${config}:${copy_command}>"
                    COMMAND_EXPAND_LISTS
                    VERBATIM)
            endif()
        endforeach()
    endfunction()
    creation_engine_stage_package_dlls(CreationEngineEditor "${CREATION_OPENXR_LOADER_LIBRARY}" 1 jsoncpp.dll)
    creation_engine_stage_package_dlls(CreationEngineEditor "${ZLIB_LIBRARY_DEBUG}" "$<CONFIG:Debug>")
    creation_engine_stage_package_dlls(CreationEngineEditor "${ZLIB_LIBRARY_RELEASE}" "$<NOT:$<CONFIG:Debug>>")
    foreach(persistence_target IN ITEMS CreationEnginePersistenceSmoke CreationEngineRocksDBRecovery
                                         CreationEnginePersistenceBenchmark CreationEnginePortablePersistenceQuery)
        creation_engine_stage_package_dlls(${persistence_target} "${ZLIB_LIBRARY_DEBUG}" "$<CONFIG:Debug>")
        creation_engine_stage_package_dlls(${persistence_target} "${ZLIB_LIBRARY_RELEASE}" "$<NOT:$<CONFIG:Debug>>")
    endforeach()
endif()

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
    Source/Views/OpenDocumentDialog.h
    Source/Views/OpenDocumentDialog.cpp
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
    Source/Frust/PodCompiler.cpp
    Source/Frust/EngineFrustHost.h
    Source/Frust/EngineFrustHost.cpp
    Source/Input/InputBindingTypes.h
    Source/Input/InputBindingDocumentStore.h
    Source/Input/InputBindingDocumentStore.cpp
    Source/Input/InputActionSystem.h
    Source/Input/InputActionSystem.cpp
    Source/Input/RawMouseInput.h
    Source/Input/RawMouseInput.cpp
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
    Source/Runtime/RuntimeDeveloperInspection.cpp
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
    Source/Animation/ProceduralLocomotion.h
    Source/Animation/ProceduralLocomotion.cpp
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

if (CREATION_ENGINE_HAS_OPENXR)
    add_executable(CreationEngineOpenXRRuntimeSmoke
        tests/OpenXRRuntimeSmoke.cpp
        Source/VR/OpenXRProvider.cpp
    )
    target_include_directories(CreationEngineOpenXRRuntimeSmoke PRIVATE
        Source
        EngineCore/include
        ${CREATION_OPENXR_INCLUDE_DIR}
    )
    target_link_libraries(CreationEngineOpenXRRuntimeSmoke PRIVATE ${CREATION_OPENXR_LOADER_LIBRARY} juce::juce_core juce::juce_graphics juce::juce_opengl)
    target_compile_definitions(CreationEngineOpenXRRuntimeSmoke PRIVATE CE_HAS_OPENXR=1)
    if (WIN32)
        creation_engine_stage_package_dlls(CreationEngineOpenXRRuntimeSmoke
            "${CREATION_OPENXR_LOADER_LIBRARY}" 1 jsoncpp.dll)
    endif()
    add_test(NAME creation_engine_openxr_runtime_smoke COMMAND CreationEngineOpenXRRuntimeSmoke)
    set_tests_properties(creation_engine_openxr_runtime_smoke PROPERTIES TIMEOUT 60)
endif()

target_link_libraries(CreationEngineEditor PRIVATE
    engine_developer_api
    juce::juce_cryptography
    Jolt::Jolt
    engine_core
    node_system
    creation_suite_material_system
    creation_suite_asset_system
    creation_suite_services
    creation_suite_ui
    creation_suite_ui_assets
    creation_suite_frust_plugin_runtime
    node_library_loader
    creation_engine_ufbx
    CreationDock
    creation_suite_node_editor_ui
    ixwebsocket::ixwebsocket
    ZLIB::ZLIB
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
    Source/Input/InputActionSystem.cpp
    Source/Input/InputBindingDocumentStore.cpp
    Source/Assets/AssetPackStore.cpp
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
    node_library_loader
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
    Source/Input/InputActionSystem.cpp
    Source/Input/InputBindingDocumentStore.cpp
    Source/Assets/AssetPackStore.cpp
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
    node_library_loader
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
)
target_include_directories(CreationEngineNodeLibraryLoaderSmoke PRIVATE Source)
target_link_libraries(CreationEngineNodeLibraryLoaderSmoke PRIVATE
    node_system
    creation_suite_frust_plugin_runtime
    node_library_loader
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
# isolated from any engine wrapper code. See Jolt_DIR
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

# Deterministic, renderer-independent foundation for grounded procedural
# locomotion. Jolt integration consumes these results in the runtime layer.
add_executable(CreationEngineProceduralLocomotionSmoke
    tests/ProceduralLocomotionSmoke.cpp
    Source/Animation/ProceduralLocomotion.cpp
)
target_include_directories(CreationEngineProceduralLocomotionSmoke PRIVATE Source)
add_test(NAME creation_engine_procedural_locomotion_smoke COMMAND CreationEngineProceduralLocomotionSmoke)

# Manual diagnostic CLI, not a ctest -- pass a real .fbx path as argv[1] to
# see exactly what FbxLoader extracted from it (mesh/material/skeleton/
# animation counts) without going through the editor's Import Hub UI.
# A real juce_add_console_app (like CreationEngineServer below), not a plain
# add_executable, because FbxLoader.h/EngineLog.h use <JuceHeader.h> --
# unlike InputActionSystem.h and friends, they were never written for the
# lean direct-module-header smoke targets.
juce_add_console_app(CreationEngineFbxImportInspect
    PRODUCT_NAME "DjehutiFBXImportInspect"
    COMPANY_NAME "LagDaemon Software"
)
juce_generate_juce_header(CreationEngineFbxImportInspect)
target_sources(CreationEngineFbxImportInspect PRIVATE
    tests/FbxImportInspect.cpp
    Source/Render/Import/FbxLoader.cpp
    Source/Diagnostics/EngineLog.cpp
)
target_include_directories(CreationEngineFbxImportInspect PRIVATE Source)
target_link_libraries(CreationEngineFbxImportInspect PRIVATE
    creation_engine_ufbx
    creation_suite_asset_system
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_opengl
)

# --- Dedicated server executable ------------------------------------------
# Headless build: no rendering or client-only assets. Only juce_core and
# juce_events provide cross-platform utilities for the server shell.
juce_add_console_app(CreationEngineServer
    PRODUCT_NAME "DjehutiEngineServer"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine.server"
)

# Keep the bundled plugins beside the server executable.
add_custom_command(TARGET CreationEngineServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CreationEngineServer>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/EngineLifecycle.frust" "$<TARGET_FILE_DIR:CreationEngineServer>/plugins/EngineLifecycle.frust"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodes.frust" "$<TARGET_FILE_DIR:CreationEngineServer>/plugins/CoreNodes.frust"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/CoreNodesLibrary.frust" "$<TARGET_FILE_DIR:CreationEngineServer>/plugins/CoreNodesLibrary.frust"
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
    Source/Frust/EngineFrustHost.cpp
    Source/Frust/PodCatalog.cpp
    Source/Frust/PodCompiler.cpp
    Source/Runtime/RuntimeWorldRunner.cpp
    Source/Runtime/RuntimeDeveloperInspection.cpp
    Source/Input/InputActionSystem.cpp
    Source/Input/InputBindingDocumentStore.cpp
    Source/Physics/PhysicsWorld.cpp
    Source/Scene/AnimatorControl.cpp
)
target_include_directories(CreationEngineServer PRIVATE Source)

target_include_directories(CreationEngineServer PRIVATE Source)

target_compile_definitions(CreationEngineServer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_INCLUDE_ZLIB_CODE=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationEngineServer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationEngineServer,JUCE_VERSION>"
)

target_link_libraries(CreationEngineServer PRIVATE
    engine_developer_api
    ZLIB::ZLIB
    juce::juce_cryptography
    Jolt::Jolt
    engine_core
    node_system
    creation_suite_asset_system
    creation_suite_frust_plugin_runtime
    node_library_loader
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(WIN32)
    creation_engine_stage_package_dlls(CreationEngineServer "${ZLIB_LIBRARY_DEBUG}" "$<CONFIG:Debug>")
    creation_engine_stage_package_dlls(CreationEngineServer "${ZLIB_LIBRARY_RELEASE}" "$<NOT:$<CONFIG:Debug>>")
endif()

add_test(NAME creation_engine_headless_ticks_smoke COMMAND CreationEngineServer --ticks 8)
set_tests_properties(creation_engine_headless_ticks_smoke PROPERTIES PASS_REGULAR_EXPRESSION "completed 8 ticks" TIMEOUT 60)

add_executable(CreationEngineDeveloperApiConformance tests/DeveloperApiConformance.cpp)
target_compile_definitions(CreationEngineDeveloperApiConformance PRIVATE JUCE_USE_CURL=0 JUCE_INCLUDE_ZLIB_CODE=0)
target_link_libraries(CreationEngineDeveloperApiConformance PRIVATE
    engine_developer_api ZLIB::ZLIB juce::juce_cryptography juce::juce_recommended_config_flags)
if(WIN32)
    creation_engine_stage_package_dlls(CreationEngineDeveloperApiConformance "${ZLIB_LIBRARY_DEBUG}" "$<CONFIG:Debug>")
    creation_engine_stage_package_dlls(CreationEngineDeveloperApiConformance "${ZLIB_LIBRARY_RELEASE}" "$<NOT:$<CONFIG:Debug>>")
endif()
add_test(NAME creation_engine_developer_api_conformance
    COMMAND CreationEngineDeveloperApiConformance "${CMAKE_CURRENT_BINARY_DIR}/developer-api-tests")
set_tests_properties(creation_engine_developer_api_conformance PROPERTIES TIMEOUT 60)

find_package(Python3 3.9 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME creation_engine_developer_api_http
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/DeveloperApiIntegration.py"
            --server "$<TARGET_FILE:CreationEngineServer>" --work "${CMAKE_CURRENT_BINARY_DIR}/developer-api-tests")
    set_tests_properties(creation_engine_developer_api_http PROPERTIES TIMEOUT 120)
    add_test(NAME creation_engine_runtime_parity_headless
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/RuntimeParityIntegration.py"
            --server "$<TARGET_FILE:CreationEngineServer>" --work "${CMAKE_CURRENT_BINARY_DIR}/developer-api-tests")
    set_tests_properties(creation_engine_runtime_parity_headless PROPERTIES TIMEOUT 90)

    if(WIN32 OR DEFINED ENV{DISPLAY})
        add_test(NAME creation_engine_runtime_parity_desktop
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/RuntimeParityIntegration.py"
                --server "$<TARGET_FILE:CreationEngineServer>" --editor "$<TARGET_FILE:CreationEngineEditor>"
                --work "${CMAKE_CURRENT_BINARY_DIR}/developer-api-tests")
        set_tests_properties(creation_engine_runtime_parity_desktop PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE)
    endif()
endif()

install(TARGETS CreationEngineEditor
    RUNTIME DESTINATION .
    RESOURCE DESTINATION .
)
install(FILES README.md LICENSE DESTINATION .)
install(FILES "${engine_httplib_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME cpp-httplib.txt)

set(CPACK_PACKAGE_NAME "DjehutiEngine")
set(CPACK_PACKAGE_VENDOR "LagDaemon Software")
set(CPACK_PACKAGE_CONTACT "LagDaemon Software")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DjehutiEngine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "creation-engine-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DjehutiEngine")
set(CPACK_GENERATOR "ZIP;WIX")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "DjehutiEngine")
set(CPACK_PACKAGE_EXECUTABLES "DjehutiEngine" "Djehuti Engine")
set(CPACK_CREATE_DESKTOP_LINKS "DjehutiEngine")

include(CPack)

# Auto-generated output name overrides
set_target_properties(CreationEngineInputActionSystemSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineInputActionSystemSmoke")
set_target_properties(CreationEngineFrustPluginSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineFrustPluginSmoke")
set_target_properties(CreationEngineFrustCodegenSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineFrustCodegenSmoke")
set_target_properties(CreationEngineEditor PROPERTIES OUTPUT_NAME "DjehutiEngineEditor")
set_target_properties(CreationEngineGenericNodeCodegenSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineGenericNodeCodegenSmoke")
set_target_properties(CreationEngineObjectBehaviorSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineObjectBehaviorSmoke")
set_target_properties(CreationEngineMultiPluginRuntimeSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineMultiPluginRuntimeSmoke")
set_target_properties(CreationEngineOpenXRRuntimeSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineOpenXRRuntimeSmoke")
set_target_properties(CreationEngineDefaultScenePhysicsSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineDefaultScenePhysicsSmoke")
set_target_properties(CreationEnginePhysicsJoltLinkSmoke PROPERTIES OUTPUT_NAME "DjehutiEnginePhysicsJoltLinkSmoke")
set_target_properties(CreationEnginePhysicsWorldSmoke PROPERTIES OUTPUT_NAME "DjehutiEnginePhysicsWorldSmoke")
set_target_properties(CreationEngineProceduralLocomotionSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineProceduralLocomotionSmoke")
set_target_properties(CreationEngineFrustGraphSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineFrustGraphSmoke")
set_target_properties(CreationEngineFrustGenericNodeReflectionSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineFrustGenericNodeReflectionSmoke")
set_target_properties(CreationEnginePhysicsNodeCodegenSmoke PROPERTIES OUTPUT_NAME "DjehutiEnginePhysicsNodeCodegenSmoke")
set_target_properties(CreationEngineCharacterControllerSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineCharacterControllerSmoke")
set_target_properties(CreationEngineNodeLibraryLoaderSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineNodeLibraryLoaderSmoke")
set_target_properties(creation_engine_ufbx PROPERTIES OUTPUT_NAME "djehuti_engine_ufbx")
set_target_properties(CreationEngineF64FfiCheckSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineF64FfiCheckSmoke")
set_target_properties(CreationEngineObjectDefinitionSmoke PROPERTIES OUTPUT_NAME "DjehutiEngineObjectDefinitionSmoke")
