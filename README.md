# Creation Engine

Creation Engine is a game engine, node-based authoring tool, and game server
built as one JUCE codebase. It is a sibling project to
[Creation Station](https://github.com/wwestlake/CreationStation).

The product scope lives in [docs/CAPABILITIES.md](docs/CAPABILITIES.md).

## Layout

- `Source/` contains the `CreationEngineEditor` client. The editor and
  runtime are the same executable in different modes.
- `Server/Source/` contains the headless `CreationEngineServer` target.
- `EngineCore/` contains the framework-independent EnTT world and simulation
  clock.
- `NodeSystem/` contains the shared, language-neutral graph model used by
  authoring domains such as materials, animation, and events.
- `BuiltInPlugins/` contains bundled FRust plugins. `EngineLifecycle.frust`
  proves the Engine FRust host and lifecycle event boundary.
- `Source/Frust/` owns the Engine-side FRust host and its native capabilities.
- `third_party/` contains submodules, including FRustLang.

## Dependencies

- **JUCE**: set `JUCE_DIR` to a local JUCE checkout.
- [`entt`](https://github.com/skypjack/entt): entity-component storage.
- **LLVM**, **Bison**, and **Flex**: required by the FRust compiler/plugin
  host supplied through the FRustLang submodule.

Clone with dependencies:

```bash
git clone --recurse-submodules <repo-url>
```

Or initialize them in an existing checkout:

```bash
git submodule update --init --recursive
```

## Building

Requires CMake 3.22+, Visual Studio 2022, and C++20.

The FRustLang submodule's own compiler build needs Flex and Bison. On a
**fresh checkout on a new machine/workspace**, CMake will not find them on
its own -- pass their location explicitly on first configure (only needed
once; it's cached after that):

```powershell
$env:JUCE_DIR = "D:\JUCE2\JUCE"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DFLEX_EXECUTABLE="D:/tools/winflexbison/win_flex.exe" `
    -DBISON_EXECUTABLE="D:/tools/winflexbison/win_bison.exe" `
    -DFRUST_WINFLEXBISON_DIR="D:/tools/winflexbison"
cmake --build build --config Debug --parallel 1 -- /m:1 /p:LinkIncremental=false
```

Adjust the `D:/tools/winflexbison` path to wherever WinFlexBison actually
lives on that machine. This mirrors the `windows-vcpkg` CMake preset already
defined in `third_party/FrustLang/projects/01_language_paradigms/02_functional/CMakePresets.json`
for FRustLang's own standalone build -- that preset is not consulted when
building Creation Engine itself (CreationEngine configures at its own root,
not through FRustLang's preset file), which is why this has to be repeated
here explicitly for every fresh Creation Engine checkout.

The Engine CMake project discovers the FRustLang submodule and builds the
shared FRust plugin runtime. A built editor copies `EngineLifecycle.frust`
beside the executable in its `plugins` directory.

## Status

The Engine shell, scene composition, OpenGL viewport, import path, dedicated
server, and FRust plugin host are active foundations. Visual FRust automation
and broader gameplay capabilities are the next authoring milestones.
