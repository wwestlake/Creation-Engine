# Zip-backed virtual file system. Depends only on juce::juce_core (no
# GUI/rendering), so both CreationEngineEditor and CreationEngineServer
# can link it — only the editor's loaders (Texture2D, ShaderComposer,
# GltfLoader) actually consume it in this pass, but the server doesn't
# need to gain a new dependency to use it once it loads assets of its own.
add_library(asset_system STATIC
    src/VirtualFileSystem.cpp
)
target_include_directories(asset_system PUBLIC include)
target_link_libraries(asset_system PUBLIC juce::juce_core)
target_compile_features(asset_system PUBLIC cxx_std_20)
