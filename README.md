# Creation Engine

Creation Engine is a game engine, node-based authoring tool, and game server
built as one JUCE codebase. It is a sibling project to
[Creation Station](https://github.com/wwestlake/CreationStation).

The product scope lives in [docs/CAPABILITIES.md](docs/CAPABILITIES.md).
The [Developer API guide](docs/DEVELOPER_API.md) covers local editor and server automation over HTTP, CLI and MCP.

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

## Building on Windows

Use the Engine submodule in the shared Creation Suite checkout and initialize
the Suite's submodules recursively. Engine needs CMake 3.22 or newer,
Visual Studio 2022 with C++20 support, and a local JUCE checkout.

Use the existing LLVM, OpenXR, ixwebsocket, zlib, and Jolt installations.
OpenXR defaults to `openxr-deps/x64-windows` next to the Suite checkout.
ixwebsocket and zlib default to the VfsService dependency tree. Jolt defaults
to the Suite's isolated `third_party/jolt-vcpkg` installation.

From the Engine directory, set the PowerShell variables below to the paths
of the installed dependencies. Both LLVM arguments point into the same
installed tree.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    "-DJUCE_DIR=$juceRoot" `
    "-DCREATION_ENGINE_LLVM_DIR=$llvmRoot/share/llvm" `
    "-DFRUST_VCPKG_INSTALLED_DIR=$llvmRoot" `
    "-DFLEX_EXECUTABLE=$winFlexBisonRoot/win_flex.exe" `
    "-DBISON_EXECUTABLE=$winFlexBisonRoot/win_bison.exe" `
    "-DFRUST_WINFLEXBISON_DIR=$winFlexBisonRoot" `
    "-DCREATION_ENGINE_IXWEBSOCKET_VCPKG_ROOT=$ixWebSocketRoot" `
    "-DCREATION_ENGINE_ZLIB_VCPKG_ROOT=$zlibRoot" `
    "-DJolt_DIR=$joltRoot/share/Jolt"
```

The ixwebsocket and zlib package roots must each contain `lib` and `debug/lib`.
For OpenXR installed elsewhere, pass `-DCREATION_OPENXR_SHARED_DEPS_ROOT`
with its SDK root.

For routine Debug editor builds, run `.\scripts\Build-EditorIncremental.ps1`.
The Windows executable, bundled plugins, and assets are placed in
`build/CreationEngineEditor_artefacts/Debug`.

## Status

The Engine shell, scene composition, OpenGL viewport, import path, dedicated
server, and FRust plugin host are active foundations. Visual FRust automation
and broader gameplay capabilities are the next authoring milestones.
