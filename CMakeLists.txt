cmake_minimum_required(VERSION 3.22)

set(CREATION_ENGINE_VERSION "0.0.1" CACHE STRING "Creation Engine product version")
project(CreationEngine VERSION ${CREATION_ENGINE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to your JUCE root directory before configuring (same JUCE checkout Creation Station uses).")
endif()

add_subdirectory("${JUCE_DIR}" juce)
add_subdirectory(third_party/entt)

# --- Shared, framework-agnostic libraries --------------------------------
add_subdirectory(NodeSystem)
add_subdirectory(EngineCore)

# --- Shared, juce_core-only libraries -------------------------------------
# Not framework-agnostic like the two above (needs juce::juce_core for
# juce::ZipFile), but still no GUI/rendering dependency, so a future
# server-side asset loader could link this without pulling in anything
# beyond what CreationEngineServer already links.
add_subdirectory(AssetSystem)

# --- Client / editor executable ------------------------------------------
# Per spec section 1, the editor and the runtime are the same binary in
# different modes — not a separate desktop app talking to the game over a
# socket.
juce_add_gui_app(CreationEngineEditor
    PRODUCT_NAME "Creation Engine"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine"
)

juce_generate_juce_header(CreationEngineEditor)

target_sources(CreationEngineEditor PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
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
    Source/Render/Scene/Vertex.h
    Source/Render/Scene/Mesh.h
    Source/Render/Scene/Mesh.cpp
    Source/Render/Scene/Material.h
    Source/Render/Scene/Material.cpp
    Source/Render/Scene/ProceduralMesh.h
    Source/Render/Scene/ProceduralMesh.cpp
    Source/Render/Shaders/ShaderComposer.h
    Source/Render/Shaders/ShaderComposer.cpp
    Source/Render/Import/GltfLoader.h
    Source/Render/Import/GltfLoader.cpp
    Source/Render/Scene/Light.h
    Source/Views/LightPanel.h
    Source/Views/LightPanel.cpp
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
)

target_link_libraries(CreationEngineEditor PRIVATE
    engine_core
    node_system
    asset_system
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# --- Dedicated server executable ------------------------------------------
# Headless build: no rendering, no client-only assets (spec section 6.2).
# Only juce_core/juce_events for cross-platform utilities (threads, time,
# argument parsing, logging) — never juce_gui_* or juce_opengl.
juce_add_console_app(CreationEngineServer
    PRODUCT_NAME "Creation Engine Server"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationengine.server"
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
    juce::juce_core
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
