# FRust Node Libraries

A node library is a plugin-owned catalog of graph nodes. It is the bridge
between a visual graph and the FRust code that implements its behavior.

## Library Contract

Every library declares:

- a stable library id, display name, description, and graph target;
- each node's stable type name, palette category, display name, and help text;
- typed input and output pins, including data, execution, or stream wiring;
- the FRust entry point used when graph compilation emits that node; and
- the host capabilities the entry point requires.

Type names are globally unique. `NodeLibraryRegistry` rejects a library that
tries to claim an existing node type, which keeps saved graph assets
unambiguous and makes plugin unload/reload ownership explicit.

## Authoring Rule

A graph stores node type names and pin instances. It does not store a second
copy of the node catalog. When a graph is opened, its plugin libraries provide
the authoritative signatures and metadata; validation detects a missing plugin
or a saved node whose pins no longer match its declared type.

The registry is backend-neutral. The Engine-side plugin loader populates it
from compiler reflection of real `node` declarations. The behavior compiler
consumes each reflected `frustEntryPoint` when it emits a FRust behavior root;
that root imports the node-library source modules with `use self::`, so graph
calls and node definitions compile together in one plugin. Material and
dataflow libraries use the same contract with their respective graph targets.

## Built-in Direction

The first libraries must be authoring primitives, not game-specific gameplay
nodes. The core palette should grow around:

- value and type conversion, arithmetic, clamp, map-range, and interpolation;
- comparisons, boolean operations, branch/select, sequence, gate, and switch;
- event flow, delays, timers, timelines, curves, and trigger/state utilities;
- collections, formatting, and other general data transformations.

Game-specific features are ordinary plugins layered over that vocabulary. A
designer or AI creates a node by writing `node pure`, `node callable`, or
`node loop` FRust; its pins and entry point are compiler-derived. The Engine
must not require a C++ change or a duplicate hand-written node descriptor.

## Source Layout

A loadable library wrapper has the ordinary embedded plugin manifest and
imports one or more sibling source modules. The node declarations belong in
those modules. Its `nodeSourceModules` list is build-layout metadata that the
compiler reflects into the Engine descriptor; graph compilation imports the
used libraries automatically rather than making an editor or AI duplicate
module filenames:

```frust
// CoreNodes.frust: manifest-bearing discovery wrapper
manifest "{..., \"nodeSourceModules\":[\"CoreNodesLibrary\"]}";
use self::CoreNodesLibrary;

// CoreNodesLibrary.frust: actual source of truth
node pure pub fn core_add(left: i64, right: i64) -> i64 = { left + right }
```

The compiler produces the descriptor transport data used by the registry and
rejects duplicate library ids or node type names before changing it. Pure,
single-value nodes already compile into native FRust calls. Callable, loop,
stream, timeline, and curve nodes share the declaration/reflection pipeline;
their control-flow lowering is the next compiler phase, not a graph interpreter
fallback.

## Single-file self-reflection (Pod Management System plan, Phase 4)

**Confirmed: a single-file plugin self-reflects its own `node` declarations.
The two-file wrapper above is a reuse/organization convention, not a
requirement of the reflection mechanism.**

Verified directly from the reflecting compiler's own source (not assumed,
not inferred from this doc), in
`third_party/FrustLang/projects/01_language_paradigms/02_functional/Codegen.h`,
`compileNodeReflection()`:

```cpp
bool compileNodeReflection(const Program& prog) {
    std::vector<const NodeDecl*> nodes;
    for (const auto* decl : prog.decls)
        if (decl->kind == DeclKind::Node) nodes.push_back(decl->nodeDecl);
    ...
```

This scans every top-level declaration in the compiled `Program` -- which
already includes anything pulled in via `use self::`, since imports are
flattened before codegen runs -- for `DeclKind::Node`, with no check on
which source file or module a declaration came from. A `.frust` file with
`node pure pub fn ...` declared directly, no `use self::`, no
`nodeSourceModules` entry at all, produces the exact same
`kFrustNodeReflectionGlobalName` LLVM global
(`FrustPluginHost.cpp::MergeNodeReflection` reads that global, and never
gates on `nodeSourceModules` for *whether* reflection happened -- only for
what to list in the resulting descriptor's informational `sourceModules`
field, which downstream graph compilation uses to know what to `use self::`
import).

**Consequence for the Pod compile pipeline (Phase 5)**: a compiled Pod that
wants to expose itself as a node does not need the two-file manifest+sibling
split -- one generated `.frust` file with the graph's output prefixed
`node pure`/`node callable` instead of plain `pub fn` is sufficient. This
was the smaller of the two branches the Pod plan flagged as possible.
