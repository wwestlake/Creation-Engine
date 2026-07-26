# The client/editor executable. Per spec section 1, this single binary is
# both the authoring tool and the shipped game — mode is a runtime switch,
# not a separate build target.
add_executable(creation_editor
    src/main.cpp
)
target_link_libraries(creation_editor PRIVATE engine_core engine_render node_system)
