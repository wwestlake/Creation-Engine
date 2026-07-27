# Creation Engine

A game engine, node-based authoring tool, and game server, built as one codebase on JUCE. Sibling project to [Creation Station](https://github.com/wwestlake/CreationStation) (LagDaemon Software).

The full capabilities specification — what the system must do, and what it deliberately doesn't — lives in [docs/CAPABILITIES.md](docs/CAPABILITIES.md). Read that first; it's the source of truth for scope.

## Layout

- `Source/` — the client/editor executable (`CreationEngineEditor`). Editor and runtime are the same binary in different modes (spec section 1), not separate applications. `Source/Render/ViewportComponent` is the JUCE OpenGL viewport the 3D scene renders into; tooling panels are composited in the same window around it.
- `Server/Source/` — the headless dedicated-server executable (`CreationEngineServer`). Links `engine_core` and `node_system` only — no JUCE GUI or OpenGL modules (spec section 6.2).
- `EngineCore/` — platform/framework-agnostic simulation core (`engine_core`: EnTT-backed world/entity state, tick clock). No JUCE dependency, so the server's core loop stays free of anything GUI-related.
- `NodeSystem/` — the unified node graph model (`node_system`: typed pins, dataflow + control-flow connections) shared by every authoring domain: animation, materials, game events/rules, audio.
- `Language/` — CEL (Creation Engine Language): the LALR(1)-parsed procedural scripting language that drives game rules/logic/state, JIT-compiled to native code via LLVM. Grammar/lexer as Flex/Bison-generated sources; `ce_lang_jit` is the only target that includes any `llvm/*.h` header (see `Language/include/lang/jit/runtime.h`'s own comment for why). `celc` is both the compiler and the project's headless test-driver binary for this module.
- `third_party/` — vendored dependencies as git submodules (see below).
- `docs/` — specs and design docs.

## Dependencies

- **JUCE** — not vendored; set `JUCE_DIR` to your local JUCE checkout, same as Creation Station (`D:\JUCE2\JUCE` on this machine). Provides the app shell, windowing, and OpenGL context (`juce_opengl`).
- [`entt`](https://github.com/skypjack/entt) (`third_party/entt`, pinned to `v3.13.2` for MSVC 2019 compatibility) — ECS storage for world/entity state.
- **LLVM** (via `vcpkg`, not vendored/submoduled) — the JIT backend for `Language/`. See "Scripting language build (LLVM via vcpkg)" below.

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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

The generator is **Visual Studio 17 2022** (switched from VS2019/v142 during GS1, to match the toolset vcpkg used to build LLVM — see the scripting section below). If `build/` was configured before this switch, its cached generator won't match; clear `build/CMakeCache.txt`, `build/CMakeFiles/`, and `build/juce/tools/CMakeCache.txt`/`CMakeFiles/` (JUCE's `juceaide` helper has its own nested cache) before reconfiguring, rather than deleting the whole `build/` directory.

This is an early scaffold: the CMake project configures and the targets are structured per the architecture above, but most capabilities in the spec are not yet implemented. Treat this as the skeleton the real work fills in, not a working engine yet.

## Scripting language build (LLVM via vcpkg)

`Language/` (`ce_lang_jit`, `celc`) needs LLVM, resolved through a vcpkg-managed install rather than vendoring LLVM's binaries or building it as part of this project's own CMake configure. Set up once per machine:

```powershell
git clone https://github.com/microsoft/vcpkg.git D:\vcpkg
D:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_DEFAULT_BINARY_CACHE = "D:\vcpkg-cache"   # keep the build cache off C: -- see note below
cd "D:\000 Creation Engine"
D:\vcpkg\vcpkg.exe install --triplet x64-windows --x-buildtrees-root="D:\vcpkg-buildtrees"
```

This reads `vcpkg.json` (pinned `builtin-baseline` = LLVM 18.1.6) and builds `llvm[core,target-x86,tools]` **from source** — vcpkg has no prebuilt binary cache for this port/feature/triplet combination, so expect a genuinely long build (hours, not minutes) the first time. It only needs to happen once; `VCPKG_DEFAULT_BINARY_CACHE` makes subsequent `vcpkg install` runs (e.g. after a triplet/feature change) reuse what's already built. Run from the repo root (where `vcpkg.json` lives), `vcpkg install` is in **manifest mode** and lands the built packages in `vcpkg_installed/x64-windows/` next to the manifest — that directory is git-ignored, and CMake auto-discovers it (see below), so no extra path variable is needed for this part.

**Then configure CMake as usual** — `JUCE_DIR` plus nothing new:

```powershell
$env:JUCE_DIR = "D:\JUCE2\JUCE"
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

This is deliberately **not** the full vcpkg CMake toolchain-file integration (`CMAKE_TOOLCHAIN_FILE`) — that variable is only honored on a build directory's very first configure, and this project's existing `build/` directory is not to be deleted/recreated without asking first (see `AGENTS.md`). Instead, root `CMakeLists.txt` checks for `vcpkg_installed/x64-windows/` next to the manifest and appends it to `CMAKE_PREFIX_PATH` so `find_package(LLVM CONFIG)` can find it — a normal, additive path that works on any reconfigure of the existing build directory, with a clear `FATAL_ERROR` (pointing back to the `vcpkg install` step above) if it's missing. Pass `-DCE_ENABLE_SCRIPTING=OFF` to skip the whole `Language/` module (e.g. on a machine without vcpkg set up yet); the editor/server build and run identically either way until GS6 wires script execution into the tick loop.

**Environment notes specific to this machine, recorded so a future session doesn't have to rediscover them:**
- Build generator is **Visual Studio 17 2022** (MSVC 14.36, Enterprise edition, installed at `D:\Program Files\Microsoft Visual Studio\2022\Enterprise` — **not** the default `C:\Program Files` location, easy to miss when checking for VS installs). The project originally configured against VS2019/v142; GS1 discovered that `vcpkg`'s LLVM build picks its own MSVC toolset independent of the project's generator, landed on VS2022's, and produced object files whose MSVC STL "vectorized algorithm" support symbols (`__std_find_trivial_*` etc.) don't exist in VS2019's runtime — a hard link failure, not a subtle bug. Switching the whole project to match was the fix; see the "Building" section above for the generator flags and how to reconfigure `build/` for the switch.
- **`C:` has very little free space (~5.7 GB)** — `D:` has hundreds of GB free. Keep vcpkg's root, buildtrees, and binary cache on `D:` (as above); do not let any of this default onto `C:`.
- The repo path (`D:\000 Creation Engine`) contains spaces and starts with a digit — this has already caused friction with naive shell quoting elsewhere in this project (import-test hooks, etc.); the same care applies to any Bison/Flex custom-command arguments added in `Language/CMakeLists.txt` going forward.

## Status

Pre-alpha scaffolding. See [docs/CAPABILITIES.md](docs/CAPABILITIES.md) for the target feature set and [Section 9](docs/CAPABILITIES.md#9-explicit-non-goals--what-this-deliberately-excludes) for what's explicitly out of scope.
