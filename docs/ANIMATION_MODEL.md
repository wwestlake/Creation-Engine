# Animation Control

**This is settled, not open for debate.** Same status as `OBJECT_MODEL.md`,
which this extends rather than replaces -- animation control is one more
instance of "the one primitive: Component," not a new tier.

## No animation blueprint, no visual state-machine DSL

There is exactly one coding system in this engine for anything that runs
before the render pipeline touches it: FRust, authored either as hand-typed
source or as a node graph that compiles down to FRust source
(`node_system::CompileBehaviorGraphToFrust` -- the graph is a compile-time
authoring layer; nothing executes a graph at runtime, the compiled output
is an ordinary FRust plugin, loaded and called like any other). A Pod is
the named, persisted unit that resolves to one such compiled plugin,
attached to an entity via `ObjectComponentKind::Pod` /
`scene::BehaviorAttachments`.

Animation control is built the same way, deliberately: a character's
animation logic -- when to switch clips, how to blend them, at what speed
-- is a Pod, exactly like anything else with behavior. There is no
separate Animation Blueprint asset type and no visual state-machine DSL.
This is a direct rejection of Unreal's Animation Blueprint model, not an
oversight -- see git history / session record for the reasoning.

`node_system::Domain::Animation` and `DataType::BoneTransform` exist in
the type system specifically for this (`shared/NodeSystem/include/node_system/node.h`,
`pin.h`) -- reserved ahead of time, now populated by
`RegisterCoreAnimationNodes` (`shared/NodeSystem/src/core_control_flow.cpp`).

## The node/host-function surface

`Source/Frust/EngineFrustHost.h`/`.cpp` expose six host-extern functions,
each operating on an explicit `entityId` (never an implicit "current
entity" beyond what `core.entity.self`/`engine_current_object_entity`
already provides), reading/writing that entity's `scene::Animator`:

- `animSetActiveClip(entity, clipName)` -- hard cut, no blend.
- `animCrossfadeTo(entity, clipName, blendMillis)` -- smooth transition.
- `animSetPlaybackSpeedPerMille(entity, speedPerMille)` -- 1000 = normal
  speed.
- `animGetActiveClipName(entity)`, `animGetClipDurationMillis(entity, clipName)`,
  `animIsBlending(entity)` -- read-only queries a locomotion Pod needs to
  make decisions (e.g. don't re-trigger a crossfade already in progress).

**Deliberately i64/string/bool at the FFI boundary, never float** --
durations cross as milliseconds, speed/scalar values as per-mille integers
(`speedPerMille`), the same convention the pre-existing (not yet
node-wrapped) `engine_set_material_scalar_parameter` already established.
No float-FFI path has ever been verified clean in this codebase (see
`RegisterCoreVariableNodes`' own comment on why Float pod-variables aren't
supported either) -- this isn't a limitation animation introduced, it's an
existing constraint animation had to design within.

Each is registered as both a callable host function
(`runtime.registerHostFunction`) and a `NodeTypeDescriptor`
(`RegisterCoreAnimationNodes`, tagged `Domain::Animation`) -- registered in
**two places**, deliberately: `EngineFrustHost`'s own constructor-time
`nodeLibraries_` (what `CompileBehaviorGraphToFrust` actually compiles
against) and `PodEditorPanel::CopyRegistry`'s palette copy (what the graph
editor's node palette actually offers). This two-call-site requirement is
a real, previously-hit gap in this codebase (see `EngineFrustHost.cpp`'s
own constructor comment) -- missing either one leaves a node type working
in one context while invisible/uncompilable in the other.

## Two clocks, deliberately split

`Animator`/`Skeleton` pose *sampling* (`Source/Scene/AnimationSampler.h`/`.cpp`,
called from `ViewportComponent.cpp`'s per-entity draw loop) runs every
rendered frame, completely independent of `isPlaying_` -- this was already
true before animation control existed, and stays true now, on purpose.
Clip *selection* and blending -- the actual decisions a locomotion Pod
makes -- only happen in a Pod's `on_tick`, which (like every other Pod)
only fires while `isPlaying_` is true, on the simulation tick
(`MainComponent::timerCallback` -> `EngineFrustHost::tick` ->
`attachedObjectBehaviors()` -> `invokeObjectHook(..., "on_tick", ...)`).

This resolves a real tension directly: "the simulation is always running"
as engine philosophy, against the practical need to freeze an NPC's
animation decisions while inspecting it. Pausing the game stops every
Pod's `on_tick` -- no more clip switching, no more crossfades starting --
while `ViewportComponent`'s render-path sampling keeps rendering whatever
pose was last commanded, smoothly, with no snap or freeze artifact.
Resuming picks the decision logic back up exactly where it left off.

## Per-entity behavior pause

`scene::BehaviorPaused` (`Source/Scene/Components.h`, a marker/tag
component, same shape as `SceneFlags::editorOnly`) freezes one specific
entity's Pods -- `EngineFrustHost::tick` skips `on_tick` for an entity
carrying it, while lifecycle hooks (`on_spawn`/`on_begin_play`/`on_end_play`)
still fire normally so a later un-pause never re-runs or skips one-time
setup, and the rest of the world (including this entity's own animation
sampling) keeps running exactly as before. Toggled per-entity from
`TransformPanel`'s "Pause Behavior (Pods)" checkbox, shown whenever the
selected entity has at least one attached Pod. This is the direct answer
to "it's hard to work on an NPC that's trying to kill things" -- freeze
just its decision-making, watch everything else (including it) keep
moving.

## Any armature, if you write the FRust for it

Import already supports any armature shape or joint-naming vocabulary,
confirmed by direct inspection: `GltfLoader.cpp`'s `ExtractSkin` (and
`AssetCatalog.cpp`'s copy into the live `Skeleton` component) resolve
hierarchy purely by node-pointer identity and copy joint names verbatim --
zero semantic name matching anywhere. Two real armature families already
in this project's assets prove this isn't theoretical: a biped
(`Humanoid_Robot`, Blender-anatomical joint names, `.L`/`.R` suffixes) and
a quadruped family (`Farm_Animal_Starter`'s Cow/Chicken/Dog/Horse, a
flatter `root/body/head/tail/leg_*` vocabulary) -- the two share no naming
convention beyond `root`/`head` matching by coincidence, and both import
and animate correctly today with zero special-casing.

Clip-based locomotion control -- Idle/Walk/Run/Crouch/... style, which is
what both existing families actually have -- needs **no joint-level
mapping at all**: clips are addressed by plain name against
`Animator.clips`, and sampling is already generic for any skeleton shape.
"Driving an armature" is just: write a Pod that knows *that* armature's
clip vocabulary and calls the nodes above with the right names. Ordinary
per-armature-family authoring, not a new mechanism.

## Deferred, named so it isn't re-explained from scratch later

- **Joint Role Mapping** -- a future per-`Skeleton` authored table mapping
  open-ended semantic role strings (e.g. `"foot_l"`) to a joint
  index/name, needed only once generic logic must address a *specific
  bone* across differently-named rigs (procedural IK, foot placement,
  look-at). Not needed for clip-based locomotion, which this plan fully
  satisfies without it. Not built, not stubbed.
- **Real character movement/input system.** Animation control gives a
  character's Pod the ability to drive its own clip/blend state; it does
  not build a velocity/input/character-controller framework. Something
  else (player input, AI) is expected to set whatever state a locomotion
  Pod reads (a Pod variable today; a real movement system's own state
  later) -- out of scope here.
- **Root motion** (`AnimationClip::rootMotion`, populated at import by
  `Scene/AnimationSlicer.h`) -- existing, dormant data, not consumed by
  anything yet.
- **MakeHuman/human-character scale verification** (a decimeter-vs-meter
  FBX risk flagged during this work's own asset audit) -- folds into
  future "realistic human characters" work, not this one.

## Applying this

Same test as `OBJECT_MODEL.md`'s own closing section: does a future
animation feature introduce a special-cased category the engine itself
enforces (an "Animation Blueprint" asset, a built-in state machine)? If
yes, it's wrong regardless of how convenient it looks -- build it as one
more Pod, one more FRust node, instead.
