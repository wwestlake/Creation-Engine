# Headless dedicated-server executable. Links engine_core (world state +
# node system) only — never engine_render — so the server build carries no
# rendering code or client-only assets (spec section 6.2).
add_executable(creation_server
    src/main.cpp
)
target_link_libraries(creation_server PRIVATE engine_core node_system)
