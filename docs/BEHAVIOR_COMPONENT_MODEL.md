# Behavior / Component Assembly

Status: design baseline for review (2026-09-02)

This document replaces any Unreal Blueprint framing used earlier in
discussion. Creation Engine does not attach one script to one Actor class.
It composes reusable component assets into a component assembly, wired
together by a graph the assembly itself owns. A Scene places scene objects
that reference those assemblies; neither Project, Game, nor Scene is itself a
component asset. See [`OBJECT_MODEL.md`](OBJECT_MODEL.md) for the canonical
container and component model, and
[`Suite-Node-Authoring-Design.md`](../../../docs/architecture/Suite-Node-Authoring-Design.md)
for the Composite-node mechanism this reuses rather than reinvents.

## 1. The three terms

**Component assembly** — a composed, reusable reference graph: component
assets, their defaults, and typed connections. An assembly does not embed its
referenced asset bytes. A Scene places a scene object that references an
assembly and carries its own transform, scene-only connections, and overrides.
The current `ObjectDefinition` format is a compatibility adapter for this
concept, not its final schema or final name.

**Component** — a reusable, pluggable asset with one or more capabilities.
A behavior-backed component adds internal control logic. Adding a component
reference to an assembly does nothing by itself until the assembly's wiring
graph connects its output pins to something. A Component's outputs mean nothing on their
own — they are only meaningful once wired to a specific target.

**Behavior** — the graph model a Component (or any node-graph-backed unit)
is authored from. A Behavior has an *inside* and an *outside*, and these
are never the same view:

- **Inside**: the graph you open and edit — nodes, wiring, control flow —
  which compiles to FRust source and then to a loadable pod (see Section
  4). This is what "editing a Behavior" means.
- **Outside**: a typed input/output pin interface, user-specified per pin
  (a real type each — Float, Bool, Vec3, whatever the type system
  supports — never a loose "any type" passthrough). This is what a
  Behavior looks like collapsed into a single node inside some other
  graph.

The outside is a contract. Anything that wants to connect to a Behavior's
exposed pins must comply with those declared types — a mismatched
connection is rejected at author time, never silently coerced. This is
the same rule the shared NodeSystem already applies to typed connections
generally.

## 2. Composite nodes are the mechanism, not a new one

A Behavior that presents as a node in another graph is exactly the
Composite/Container node already specified in
`Suite-Node-Authoring-Design.md`: a node instance with external pins that
is *also* a graph asset containing its own internal nodes and
connections, nestable to any depth, with an explicit, versioned,
independently-editable interface (renaming a pin does not break existing
links). Component-on-a-Model is that same mechanism applied to attaching
plugin behavior to an object, not a parallel system.

## 3. Wiring a Component into an Assembly

1. Reference: add a component-asset reference to an assembly.
2. Wire: in the assembly's own wiring graph, connect the Component's output
   pins to specific targets the assembly exposes — ordinary nodes with their
   own typed pins (e.g. "this wheel mesh's rotation," "the chassis
   transform"). These target nodes are built per Model as needed; there is
   no separate mechanism required for a Model to expose connection
   points beyond the same typed-pin/typed-node contract Components
   already use.

A component assembly with no behavior references may be presentation-only. A Component
attached but unwired does nothing. Both states are valid and expected
during authoring.

## 4. Compilation: what gets saved, what gets cached, what ships

The graph is the model — the thing saved, reopened, and edited. Nothing
else is. Following the same split Materials already use
(`Material::savedGraphSource` vs. `compiledMaterialSource`):

- **Saved**: the Behavior's graph (`.frgraph` serialization, same format
  Materials already use).
- **Compiled on demand, not saved by default**: FRust source text,
  generated from the graph. Ephemeral — regenerated whenever the graph
  needs to produce a fresh compile, never hand-edited, never itself the
  thing reopened for editing.
- **Cached when actually needed for loading**: the compiled pod (a real
  native plugin binary, produced by FRust's LLVM-based compiler). This is
  the artifact `EngineFrustHost::loadObjectBehavior` loads. It is cached
  specifically so the running game never has to invoke the compiler
  itself — the same reason a GPU program gets compiled once rather than
  every frame, except FRust's compiled form, unlike GLSL, does not need
  recompiling per target machine at all: it ships as-is.

This differs from GLSL in exactly one respect worth keeping explicit:
GLSL *must* be compiled at runtime by whatever GPU driver is running it —
there is no way to ship a pre-compiled GLSL program portably. FRust has
no such constraint; the compiled pod is the real ship artifact.

Per [`CAPABILITIES.md`](CAPABILITIES.md) 4.1, editing a graph while the
game or scene is running must update behavior in under a second with no
rebuild-and-relaunch cycle. This means the compile step must support
replacing an already-loaded pod in place, not only a first load — a
requirement on `EngineFrustHost`'s load path, not just on the editor UI.

## 5. Current state vs. this design

Real and already built, verified in code, not assumed:

- `ObjectDefinitionRef`/`BehaviorAttachments` (`Source/Scene/Components.h`)
  — an entity references a definition and carries a list of attached pod
  IDs. Persisted by `EngineSceneSerializer`.
- `EngineFrustHost` — loads pods by ID, dispatches `on_spawn`/
  `on_begin_play`/`on_tick`/`on_end_play`/`on_destroy` per attached pod
  per entity, exactly as documented in `FRUST_BEHAVIOR_LIFECYCLE.md`.
- `FrustLogicPanel` — a real node-graph editor (palette, canvas,
  inspector) that compiles a graph to FRust source via
  `CompileBehaviorGraphToFrust`.
- FRust's own LLVM-based compiler — full working `if`/`else`/`while`/
  `for`/`break`/`continue` codegen, verified directly in
  `third_party/FrustLang`'s `Codegen.h`. Control flow is not missing from
  the language.

Missing, specifically:

- `CompileBehaviorGraphToFrust` (`shared/NodeSystem/src/frust_codegen.cpp`)
  explicitly refuses `core.branch`/`core.for`/`core.while` — the node
  types are registered (`core_control_flow.cpp`) but the graph→FRust-text
  step has no structured lowering for them yet. This is the one place
  the language's real control-flow support does not yet reach.
- No save/list UI for Behaviors — `FrustLogicPanel` edits one anonymous
  graph, not a named, reopenable asset.
- No compile→loadable-pod pipeline from the editor — compiling only
  displays generated FRust text; nothing invokes the FRust compiler
  toolchain or writes a loadable binary.
- No Component concept at all yet — no attach-to-Model UI, no per-Model
  wiring graph, no Component instance list.
- No Composite-node implementation yet in the shared NodeSystem editor,
  which this design depends on for "a Behavior collapses into a node
  elsewhere."

## 6. Open questions, not decided here

1. **Execution context.** `CAPABILITIES.md` 2.2/4.4 requires the same
   graph language to eventually run identically whether client-side
   (cosmetic) or server-side (authoritative) — a third context beyond
   editor-time. No server/multiplayer infrastructure exists yet. This
   design scopes to editor + client execution only; server-authoritative
   execution is real, documented, future work, not silently dropped.
2. **Asset-tracking depth.** `ENGINE_ASSET_MANAGEMENT_PLAN.md` specifies
   that FRust-authored behavior becomes a managed code asset with
   dependency tracking, the same system Phases 0-4 of the asset pipeline
   already built for models/textures/audio. Whether Behaviors/Components
   go through that full system now or attach-to-object ships first with
   asset-tracking following separately is not decided here.

## 7. Delivery order

Every item below follows the same cycle, stated explicitly per the
user's standing instruction (2026-09-02): start the item on a clean
working tree, build at the end (iterating on failures until it
succeeds, never committing a broken build), commit/push/PR only on
success, wait for review and merge, then sync and confirm the tree is
clean again before starting the next item.

1. **Done** (Phase 4.5, 2026-09-02). Structured control-flow lowering in
   `frust_codegen.cpp` for Branch/Sequence/For/While/Break/Continue/Return.
2. **Done** (Phase 4.5). Save/load a Behavior graph as a named,
   reopenable asset (`BehaviorCatalog`, `FrustLogicPanel`'s Browse/Edit
   redesign) -- via a `.frgraph` serialize/deserialize round-trip, not a
   direct copy, since `node_system::Graph` move-assigns only.
3. **Done** (Phase 4.5). Compile → produce a real loadable pod, cached,
   not regenerated by the running game -- today this is the generated
   FRust source cached as a `.frust` file and loaded through the same
   `PluginRuntime::load()` JIT path `loadBundled()` already uses, not
   yet the AOT-compiled native binary via `frust_compiler.exe` this
   section originally envisioned (that CLI contract was never verified;
   see section 4's note on this).
   - **Also shipped**, as a stand-in for the attach half of item 4:
     `BehaviorAttachmentPanel`, attaching a compiled Behavior's pod ID
     directly to a selected entity's `BehaviorAttachments`. This is
     entity-level attachment, not the Component/Model wiring graph item
     4 describes -- still real progress, not the full design.
4. Component concept: attach a Behavior-backed Component to a Model,
   with a per-Model wiring graph connecting Component outputs to Model
   targets.
5. Composite-node collapse: a Behavior/Component usable as a single node,
   with its typed interface, inside another graph.
6. Hot-reload: replace an already-loaded pod without stopping Play, per
   `CAPABILITIES.md`'s live-iteration requirement.
