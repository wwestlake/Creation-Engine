# Creation Engine

A game engine, node-based authoring tool, and game server, built as one codebase on JUCE. Sibling project to [Creation Station](https://github.com/wwestlake/CreationStation) (LagDaemon Software).

The full capabilities specification — what the system must do, and what it deliberately doesn't — lives in [docs/CAPABILITIES.md](docs/CAPABILITIES.md). Read that first; it's the source of truth for scope.

## Layout

- `Source/` — the client/editor executable (`CreationEngineEditor`). Editor and runtime are the same binary in different modes (spec section 1), not separate applications. `Source/Render/ViewportComponent` is the JUCE OpenGL viewport the 3D scene renders into; tooling panels are composited in the same window around it.
- `Server/Source/` — the headless dedicated-server executable (`CreationEngineServer`). Links `engine_core` and `node_system` only — no JUCE GUI or OpenGL modules (spec section 6.2).
- `EngineCore/` — platform/framework-agnostic simulation core (`engine_core`: EnTT-backed world/entity state, tick clock). No JUCE dependency, so the server's core loop stays free of anything GUI-related.
- `NodeSystem/` — the unified node graph model (`node_system`: typed pins, dataflow + control-flow connections) shared by every authoring domain: animation, materials, game events/rules, audio.
- `third_party/` — vendored dependencies as git submodules (see below).
- `docs/` — specs and design docs.

## Dependencies

- **JUCE** — not vendored; set `JUCE_DIR` to your local JUCE checkout, same as Creation Station (`D:\JUCE2\JUCE` on this machine). Provides the app shell, windowing, and OpenGL context (`juce_opengl`).
- [`entt`](https://github.com/skypjack/entt) (`third_party/entt`, pinned to `v3.13.2` for MSVC 2019 compatibility) — ECS storage for world/entity state.

Clone with submodules:

```bash
git clone --recurse-submodules <repo-url>
```

Or, if already cloned:

```bash
git submodule update --init --recursive
```

## Building

Requires CMake 3.22+ and a C++20 compiler (MSVC on Windows).

```powershell
$env:JUCE_DIR="D:\JUCE2\JUCE"
cmake -S . -B build
cmake --build build --config Debug
```

This is an early scaffold: the CMake project configures and the targets are structured per the architecture above, but most capabilities in the spec are not yet implemented. Treat this as the skeleton the real work fills in, not a working engine yet.

## Status

Pre-alpha scaffolding. See [docs/CAPABILITIES.md](docs/CAPABILITIES.md) for the target feature set and [Section 9](docs/CAPABILITIES.md#9-explicit-non-goals--what-this-deliberately-excludes) for what's explicitly out of scope.
