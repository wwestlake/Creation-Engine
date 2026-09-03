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

## Where the current implementation still violates this

`ObjectDefinition` (`Source/Scene/ObjectDefinitions.h`) hard-codes three
separately-typed slots -- `meshAssetId` (a field), `behaviorPods` (its own
list), `children` (another list) -- instead of one uniform list of typed
component entries. That is the exact arbitrary category distinction this
model rejects, reintroduced one level above the ECS runtime, which
already gets this right at the data level (an `entt::entity` is genuinely
just an orthogonal bag of components with no inherent type). Collapsing
`ObjectDefinition` down to "a list of components" is the concrete,
implied fix here -- not yet done.

The current "object definitions are recipes, object instances are live
entities created from a definition" split (below) describes today's real
code, not the target model. It should be read as the mechanism that
needs to be reshaped to fit the model above, not as a second, competing
description of what's correct.

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
