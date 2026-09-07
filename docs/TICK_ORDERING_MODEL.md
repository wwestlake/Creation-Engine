# Tick Ordering & Cross-Pod Write Safety

## The problem

`core.entity.findByName` (and the future object-instance Schematic node) let one
Pod hold a reference to a *different* entity's data, not just its own. Combined
with the physics/attach nodes (`core.physics.applyForce`, `applyImpulse`,
`setLinearVelocity`, direct `RigidBodyComponent`/`ColliderComponent` edits),
this makes a real, reachable hazard: two Pods can now read and write the same
dynamic entity's state in the same tick, with no guarantee about which one's
write "wins" or which one's read sees stale-vs-fresh data. Static entities
(nothing changes their state tick to tick) aren't at risk -- there's nothing to
race against. Dynamic, physics-driven entities referenced by more than one Pod
are the real case this needs to cover.

## What Unreal does (researched directly against Epic's source, not copied)

Not a runtime read/write lock or permission-grant system. Two tiers instead:

1. **Tick groups**: a small, fixed, ordered set of phases, defined relative to
   the physics step (pre-physics, during-physics, post-physics, plus a couple
   more for things that must run last). Every actor/component declares which
   group it belongs to (most default to pre-physics). Every member of one
   group finishes before the next group starts. This alone answers "does my
   code need to run before or after this frame's physics resolves" for the
   overwhelming majority of cases, for free, with no per-actor bookkeeping.
2. **Tick prerequisites**: for the narrower case where two specific
   actors/components need a guaranteed order *within* (or across) a tick
   group, one can declare "wait until that other one has ticked this frame."
   This is a real, per-actor dependency edge, not a coarse group -- and it's
   only paid for when the group ordering doesn't already guarantee the order,
   which the engine actively checks for and skips otherwise.

The result: races are prevented by deterministic ordering, decided ahead of
time, not detected/arbitrated at the moment of conflict.

## What this means for us

We don't have Unreal's parallel task-graph scheduler, and don't need it --
Pods tick single-threaded, one at a time, per frame. The two-tier *shape*
still applies, just resolved much more simply:

- **Phase (already informally exists, worth naming for real):**
  `MainComponent::timerCallback()` already runs Pod `on_tick` logic
  (`Simulation::Step`/`FoundationGameplay::Step`) before `PhysicsWorld::
  Advance()`, and collision-event dispatch (`frustHost_.tick()`) after. That's
  already a two-phase, physics-relative split -- it just isn't named or
  guaranteed as a real contract yet, and there's no way for a Pod to declare
  "run me in the later phase" if it specifically needs to react to this same
  tick's physics results.
- **Per-Pod tick prerequisite (not yet built):** a Pod could declare "tick me
  after `<other Pod on this entity>`" or "tick me after `<referenced
  entity's own Pod>`," resolved once per tick via a plain topological sort
  over whatever Pods declared a prerequisite this frame (most Pods declare
  none, and just run in their existing phase, same cost as today). No locks,
  no read-write grants, no runtime arbitration -- the same "solved by
  deterministic order, decided ahead of time" answer Unreal's model gives,
  sized for a single-threaded scheduler instead of a parallel one.

## Explicitly not decided yet

- Whether prerequisites are declared in the Schematic graph itself (a new
  node/pin) or as Pod-level metadata set outside the graph.
- Whether a dependency cycle is a hard compile-time error, a runtime warning
  with an arbitrary tie-break, or something else.
- Whether the phase split needs more than two named phases, or two (pre-
  physics / post-physics) covers everything real Pods will need.

Named here so the direction survives between sessions -- not yet planned in
implementation detail, not yet built.
