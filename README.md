# Creation Engine

A game engine, node-based authoring tool, and game server, built as one codebase. Sibling project to Creation Station.

The full capabilities specification — what the system must do, and what it deliberately doesn't — lives in [docs/CAPABILITIES.md](docs/CAPABILITIES.md). Read that first; it's the source of truth for scope.

## Layout

- `engine/` — platform-agnostic simulation core (`engine_core`: world/entity state, no rendering) and the rendering/windowing layer (`engine/render`, `engine_render`: bgfx + GLFW + Dear ImGui). The dedicated server links only `engine_core`.
- `node_system/` — the unified node graph model (typed pins, dataflow + control-flow connections) shared by every authoring domain: animation, materials, game events/rules, audio.
- `editor/` — the client/editor executable. Editor and runtime are the same binary in different modes, not separate applications.
- `server/` — the headless dedicated-server executable.
- `third_party/` — vendored dependencies as git submodules (see below).
- `docs/` — specs and design docs.

## Dependencies

Vendored as git submodules under `third_party/`:

- [`bgfx.cmake`](https://github.com/bkaradzic/bgfx.cmake) — CMake build for bgfx/bx/bimg, the cross-platform GPU abstraction (targets D3D11/12, Vulkan, Metal, GL from one API).
- [`glfw`](https://github.com/glfw/glfw) — windowing and input.
- [`imgui`](https://github.com/ocornut/imgui) — immediate-mode tooling UI, overlaid on the 3D viewport.
- [`entt`](https://github.com/skypjack/entt) — entity-component-system storage for world/entity state.

Clone with submodules:

```bash
git clone --recurse-submodules <repo-url>
```

Or, if already cloned:

```bash
git submodule update --init --recursive
```

## Building

Requires CMake 3.20+ and a C++20 compiler (MSVC on Windows).

```bash
cmake -S . -B build
cmake --build build
```

This is an early scaffold: the CMake project configures and the targets are structured per the architecture above, but most capabilities in the spec are not yet implemented. Treat this as the skeleton the real work fills in, not a working engine yet.

## Status

Pre-alpha scaffolding. See [docs/CAPABILITIES.md](docs/CAPABILITIES.md) for the target feature set and [Section 9](docs/CAPABILITIES.md#9-explicit-non-goals--what-this-deliberately-excludes) for what's explicitly out of scope.
