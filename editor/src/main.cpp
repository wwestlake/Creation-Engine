#include <cstdio>

#include "engine/render/window.h"
#include "engine/world.h"
#include "node_system/graph.h"

int main() {
    ce::engine::render::Window window(1600, 900, "Creation Engine");
    ce::engine::World world;

    // Proves the node system links into the same binary that owns the
    // viewport — the editor's graph editors operate on Graph instances
    // like this one, bound to entities/systems in `world`.
    ce::node_system::Graph exampleGraph("untitled");

    std::printf("Creation Engine editor started.\n");

    while (!window.ShouldClose()) {
        window.PollEvents();
        world.AdvanceTick();

        window.BeginFrame();
        window.EndFrame();
    }

    return 0;
}
