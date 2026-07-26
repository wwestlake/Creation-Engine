# Platform-agnostic simulation core: entity/world state, node-graph
# execution hookup. No rendering here — this is what the headless server
# build links against (spec section 6.2), plus the client/editor.
add_library(engine_core STATIC
    src/world.cpp
)
target_include_directories(engine_core PUBLIC include)
target_link_libraries(engine_core PUBLIC EnTT::EnTT node_system)
target_compile_features(engine_core PUBLIC cxx_std_20)

# Rendering + windowing. Only the client/editor executable links this —
# the dedicated server build never does (spec section 6.2: "no rendering,
# no client-only assets").
add_subdirectory(render)
