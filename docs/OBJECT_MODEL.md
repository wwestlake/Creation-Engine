# Creation Engine Object Model

**This is settled, not open for debate.** The user described this directly,
repeatedly, across many attempts, until it landed precisely. A future
session reading this: don't re-derive it, don't second-guess whether it's
serious, don't ask to confirm again. Apply it, and move fast.

## Why this, instead of the conventional pattern

This is not novelty for its own sake. It's an attempt to find the actual
minimal, principled shape of "a game" -- not to inherit boundaries other
engines have (Unreal's Actor vs. ActorComponent, Unity's GameObject vs.
Component, a privileged Scene/World type) just because they're
conventional. Those boundaries exist for those engines' own historical
and practical reasons, not because they're structurally necessary. Judge
every one of them on whether it's actually required, not on precedent.

## Hierarchy is a view, not the substrate

Nesting/containment (a tree, an outliner, parent/child) is a projection
for human reasoning -- a convenient way for a person to look at part of
the system -- not what the underlying data structure has to be. The real
substrate is a graph: components as nodes, relationships as typed edges.
"Houses" is one kind of edge. A wired connection (component A's output to
component B's input) is another kind of edge, sitting on the same
substrate. Nothing requires every relationship in the system to reduce
cleanly to a tree; a tree view is just one useful rendering of the graph
when containment happens to be the relationship worth looking at.

## The one primitive: Component

There is exactly one concept in this engine: the **component**. Nothing
else exists as a distinct kind of thing -- not "GameObject," not "Scene,"
not "World." Those are not engine concepts.

A component may, independently and optionally:

- carry a **position** (and orientation/scale) -- some do, some don't. A
  component with no position is pure behavior/logic, with no spatial
  meaning at all.
- **house other components** -- a component can contain other components,
  which act/render relative to the housing component's position, if it
  has one.

That's the whole model. Recursive, uniform, all the way up and all the
way down. A game is a big component, full of components, some of which
house other components, some of which have position, some of which are
pure behavior with neither.

## There is no privileged tier

"GameObject," "Scene," "World," "Level" are labels a *game* built on this
engine chooses to apply to a component at whatever point of nesting or
scale is meaningful to that game -- the engine itself must never treat any
of them as a distinct type. The same way Minecraft calls one top-level
component "the Overworld" and another "the Nether": a game can have many
of what it calls "Worlds" because "World" was never a privileged
singleton to the engine to begin with.

Concretely: `engine::World` (a real, capitalized, singular class in this
codebase today) bakes in exactly the wrong assumption -- that "World" is
an engine-level primitive rather than game vocabulary applied to an
ordinary component instance. This needs to be corrected, not preserved
as-is just because it already exists.

## Behaviors (Pods) are components, not a special case

A FRust pod is exactly a component, the same tier as a mesh reference or
a physics component -- not a second-class "attached behavior" bolted onto
a "real" object. Any component can carry pod-behavior. Nothing about
having a position or housing children is a prerequisite for having
behavior, and nothing about having behavior implies a position.

## Entity, Thing, Object -- and where Project/Game/Scene sit

This refines "the one primitive: Component" above, it doesn't replace it.
Component is the mechanism. Entity, Thing, and Object are what a human
calls a component depending on how much of that mechanism it has actually
taken on -- three rungs on one ladder, not three competing ideas.

**Entity**: the top-level abstraction. A name, and nothing else
guaranteed. Pure identity.

**Thing**: an Entity that has gained real spatio-temporal extent. This is
literal, not metaphor -- a real memory footprint, a real duration, real
causal power within whatever process holds it. A Thing genuinely exists,
the same way a running piece of code genuinely occupies memory for a real
span of time and does real work in that time.

**Object**: a Thing that has taken on a specific characteristic set. Not a
privileged tier -- "Object" is a badly overloaded word elsewhere (a class,
an instance, an elementary particle of some type system, depending who's
using it), so it's deliberately narrowed here to mean exactly this: a Thing
plus whichever bundle of characteristics got applied. A mesh reference is a
Thing with mesh-characteristics. A material is a Thing with
material-characteristics. A stop sign is a Thing composed from several
other Things (a model, a material, maybe a behavior). None of these is a
lesser category than another -- same non-negotiable point "There is no
privileged tier" above already makes about GameObject/Scene/World, applied
one level down to "Object" itself.

A container variant exists: some Things exist specifically to hold an
arbitrary number of other Things, with FRust as the glue that wires the
contents together. This is not a third kind of edge -- it's the "houses"
edge from "Hierarchy is a view, not the substrate" above, at a coarser
grain, plus real FRust-level connections (the wired-connection edge, same
section) between whatever's inside. Composition itself reuses the exact
mechanism already built for `ObjectDefinition`'s uniform component list --
`ObjectComponentKind::{Mesh, Pod, Child}`, one list, tagged by kind. No new
composition mechanism is introduced by any of this.

Where this sits, one level up from the engine itself: **Project** is the
real top-level Thing, spanning every application in the Creation Suite --
Creation Engine among several others. Each **application** is a tool
operating on Things within its own area of the Project, not the Project
itself. The editor window is a *view into* the Project, not identical to
whatever it's currently showing -- the same relationship "hierarchy is a
view, not the substrate" already establishes, recurring one level up at a
different scale. Creation Engine specifically manages **Game**-Things:
exactly one open at a time in the live editor, but every Game-Thing that
exists in the Project is listable and reopenable. **Scene**-Things get the
same treatment one level inside a Game.

The operational test for Thing-hood: can you create it, list it, open it,
save it, delete it, rename it -- the exact verb surface `PodCatalog` and
`ObjectDefinitionCatalog` already give Pods and Object Definitions. As of
this writing, Game and Scene do not clear that bar -- they can only be
created, then nothing else. Fixing that is the subject of a companion
implementation plan, not a documentation exercise; this section records
the settled reasoning for why it matters, not the mechanics of the fix.

## Where the current implementation matches this (fixed)

`ObjectDefinition` (`Source/Scene/ObjectDefinitions.h`) used to hard-code
three separately-typed slots -- `meshAssetId` (a field), `behaviorPods`
(its own list), `children` (another list). That was the exact arbitrary
category distinction this model rejects. It's been collapsed into one
uniform `std::vector<ObjectComponentEntry>`, tagged by
`ObjectComponentKind::{Mesh, Pod, Child}` -- a mesh reference, an attached
Pod, and a nested child object are now the same kind of list entry.

## Multi-part import decomposes into components, not one blob

A source file with multiple mesh parts (each with its own transform
relative to a parent, e.g. a vehicle model with 26 separate pieces) is
not one mesh -- it's a component housing multiple mesh-components, each
carrying its own position. Import reflects this directly: a multi-node
model produces one Object Definition with one Mesh-kind component per
mesh-bearing node, each with its own relative transform, rather than
flattening everything into a single mesh (the old, wrong behavior -- it
silently discarded every part past the first).

Two accepted limitations of this, named here so they stay tracked rather
than rediscovered later:

- **Node-index addressing is order-dependent.** A part is addressed as
  "node N of asset X." Re-exporting a file with reordered nodes silently
  repoints existing placed instances at the wrong part. A node name is
  stored as a display/fallback value, but there's no automatic
  reconciliation-by-name.
- **`composeTransform` is additive, not a real matrix composition** -- it
  adds positions and rotations and multiplies scale; it does not rotate a
  child's offset by its parent's orientation. This was already a latent
  approximation for nested Child definitions; multi-part import exposes
  it far more often, for any source rig with a rotated intermediate node.
  A real fix is matrix-based composition -- future work, not yet built.

## Today's mechanism (real, working, not yet reshaped)

- An object definition does not own a GPU resource or raw C++ pointer. It
  stores an asset identifier. The viewport resolves that identifier
  through `AssetCatalog` once it has a live OpenGL context.
- Definitions can compose children. The factory detects direct and
  indirect cycles, while allowing the same child definition to be reused
  in separate branches.
- Definition identity, asset references, behavior references, and
  per-instance state are serialized with the scene.
- During a behavior call, `engine_current_object_entity()` identifies
  only the object receiving that call. The behavior can use a separately
  granted Engine capability such as `engine_set_position_x`; it is not
  given unrestricted registry access. Pod source discovery and VFS-backed
  project packaging are a separate asset-management concern from this
  execution contract.

The Engine's named behavior lifecycle is documented in
[`FRUST_BEHAVIOR_LIFECYCLE.md`](FRUST_BEHAVIOR_LIFECYCLE.md).

## Applying this

Every future addition to the object/scene system is judged against this:
does it introduce a special-cased category ("this is a GameObject, that's
a Pod, that's a Scene") that the engine itself enforces? If yes, it's
wrong regardless of how convenient it looks, and should be built as one
more instance of the single component concept instead.
