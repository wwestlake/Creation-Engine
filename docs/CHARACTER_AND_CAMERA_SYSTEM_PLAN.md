# Character, Puppet, and Camera System Plan

## Purpose

Build one character system that can support first person, follow camera, top
down, detachable fly/drone, cinematic, and VR views without making separate
"FPS character," "third-person character," and "top-down character" asset
families. A game chooses which capabilities a player may use; the Engine
keeps the underlying character and camera systems general.

This is intentionally a composition model:

* A **Character Definition** is reusable authored data: body, visual recipe,
  skeleton contract, physical dimensions, capabilities, animation references,
  and default behavior references.
* A **Character Instance** is one placed character in a scene: transform,
  current state, inventory/progression references, and a link to its
  Character Definition.
* A **Puppet Runtime** is the live control adapter for a possessed instance.
  It turns normalized player/AI/network intent into movement, interaction,
  animation parameters, and camera targets. "Player Puppet" remains a
  candidate editor-facing label; it is not being forced into every internal
  type before its final responsibility is proven.
* A **Camera Director** owns view selection and camera behavior. It observes
  or follows a puppet; it is never baked into the character definition.
* A **Possession** links a controller to a puppet subject for a bounded time.
  The subject may be a character, vehicle, robot, animal, turret, or drone.

## Design Rules

1. One actor may be viewed through any camera mode without being respawned,
   converted, or given a different animation/controller implementation.
2. Cameras and control permissions are separate. A game can allow only
   follow camera, while editor, photo, VR, and cinematic tools retain access
   to the full camera family.
3. Input describes intent, not a particular camera. `move`, `look`, `jump`,
   `interact`, `sprint`, and `use` are normalized actions. The active camera
   mode maps intent to world motion, but the puppet receives one coherent
   movement request.
4. Reusable assets never contain live EnTT entity handles. Definitions use
   stable asset ids; instances use stable scene-instance ids; runtime objects
   resolve these at play start.
5. FRust Pods own authored game behavior. The native runtime owns safe,
   deterministic bridges to input, physics, animation, cameras, and scene
   lifetime.
6. Every transition is reversible: leaving first person, unpossessing a
   vehicle, returning a fly camera, or ending a cinematic restores a known
   prior controller/camera state rather than relying on cleanup by accident.

## Character Definition Asset

`CharacterDefinition` becomes a first-class Suite asset kind, saved one
definition per asset in the project's VFS and listed in Content Browser. It
is not an Engine-private XML file hidden beside a scene.

The initial asset schema should contain:

* Stable id, display name, description, schema version, tags, and provenance.
* Base object/visual definition reference, pinned or latest-version policy,
  and canonical skeleton identifier.
* Physical shape: capsule radius, capsule half-height, step height, mass
  policy, walk/run/crouch/jump defaults, and collision layer.
* Appearance parameters: body-shape inputs, skin/hair references, and a list
  of garment/equipment slots. These remain data, not a requirement to
  regenerate a mesh at runtime in the first milestone.
* Capability references: locomotion, inventory, interaction, health, AI, and
  vehicle/seat compatibility. A definition declares what it supports; a game
  decides which capabilities it enables.
* Animation set and optional animation-controller Pod reference.
* Named rig-anchor contract used by cameras, equipment, interaction, and VR.

Existing `CharacterDefinition` serialization is a useful seed, but it is not
yet an asset catalog type or a scene reference. The first implementation must
preserve its readable schema rather than throw it away.

## Scene and Runtime Components

### Authored Scene Components

* `CharacterDefinitionRef`: durable reference to a Character Definition.
* `CharacterInstance`: stable instance id plus scene-owned state such as
  current health, equipped item ids, and game-specific saved values.
* `PossessionSpawn`: optional scene marker identifying an instance that a
  local client should possess at game start. The current `PlayerSpawn`
  prototype is a transitional form of this idea and must not become a second
  permanent character path.
* `CameraAnchorOverrides`: optional per-instance adjustments for a tall hat,
  unusual body, vehicle seat, or a special cinematic mount.

### Runtime Components

* `PuppetController`: the active movement/interaction bridge for one entity.
* `CharacterMotionState`: resolved velocity, grounded state, stance, and
  locomotion state read from Jolt rather than guessed from input.
* `PossessionState`: controller id, subject instance id, authority, active
  input context, and restoration state.
* `CameraDirectorState`: active mode, subject, current blend, effect stack,
  and optional detached-camera state.

These are intentionally separate: a scene character can exist without a
player controller, and a camera can exist without being attached to one.

## Camera Director

The Camera Director runs after physics has resolved the puppet for the frame
and before render snapshot publication. It consumes the final interpolated
anchor transforms, so the camera does not lag an animated or physics-driven
character by a full frame.

Supported modes:

* **First person:** attach to an eye/head anchor, retain pitch/yaw, and hide
  selected own-body geometry only in this camera's render mask.
* **Follow:** orbit a pivot around chest/pelvis/head anchors; apply spring
  smoothing, occlusion raycasts, shoulder offsets, and distance limits.
* **Top down:** follow an elevated pivot with game-configurable rotation,
  zoom, pan constraints, and framing.
* **Detached fly/drone:** release the camera while preserving an optional
  follow target, maximum range, collision policy, battery/permission rule,
  and explicit return-to-subject behavior.
* **Free editor:** never mutates gameplay state and can be active only in
  edit/developer contexts.
* **Cinematic/recording:** accepts a path, external director commands, or a
  named scene camera. Djehuti Movie may request camera output but cannot take
  input authority without an explicit handoff.
* **VR:** head pose drives a VR camera rig; hands/controllers remain an input
  context, not a separate avatar class.

Camera effects form an ordered stack: recoil, head bob, breathing, damage
sway, shake, FOV changes, depth of field, and scripted shots compose rather
than each system overwriting a camera transform.

## Blender and Rig Contract

The Blender bridge remains the path from a comfortable modeling tool into the
VFS. Before asset production begins, publish a versioned canonical rig
contract from the Engine and have the add-on validate it before export.

The contract must specify:

* Accepted format, coordinate convention, units-per-meter, scale tolerance,
  bind-pose expectation, and ground/contact convention.
* Required deform hierarchy and a stable canonical skeleton id.
* Required named anchors, whether they are bones or named empties, for root,
  pelvis, chest, head, left/right eyes, left/right hands, feet, equipment
  sockets, and generic camera mounts.
* Accepted animation clip naming and duration rules; clips contain actual
  changing curves, not empty named slots.
* Mesh/skeleton alignment, vertex weight limits, and a validation report
  containing measured height, ground offset, non-unit object scale, missing
  anchors, and out-of-range weights.

The first MakeHuman character is a compatibility specimen, not a production
template. Preserve the editable MakeHuman source, export a rigged copy to
Blender, then record its actual height, scale, bind pose, bone names, and
weight quality before deciding the retarget/import procedure.

## Clothing and Equipment

Clothing is a modular visual/equipment system, not permanently fused geometry
for each body variant.

Milestone-one garments are separate skinned meshes that reference the same
canonical skeleton as the body. They carry transferred/painted weights and
occupy named slots such as torso, legs, feet, head, hands, outerwear, and
backpack. Equipment uses rig anchors/sockets where it should follow a bone
rather than deform as cloth.

Body hiding is data-driven coverage: an outfit marks body regions/material
sections it occludes. Do not destructively delete body faces from the master
character just to make one outfit work. This keeps one body usable for many
outfits and avoids the MakeHuman "hide faces under clothes" option becoming a
pipeline dependency.

Later work may add morph correction, automatic garment fitting, layered
collision, cloth simulation, and physics-driven accessories. Those are not
requirements for a believable first character. The first quality gate is a
shirt, pants, footwear, and a rigid carried item that survive an idle, walk,
run, jump, crouch, and arm-raise stress sequence without obvious clipping.

## Implementation Order

1. **Baseline and audit.** Finish the current Engine build; run the existing
   physics smoke test; inventory the canonical robot, current Blender bridge,
   and MakeHuman specimen without modifying their source data.
2. **Asset foundation.** Add the Suite-wide `characterDefinition` asset kind,
   its storage/display token, project catalog save/load path, and Content
   Browser representation. Add round-trip tests before an editor panel.
3. **Definition-to-instance path.** Add `CharacterDefinitionRef` and
   `CharacterInstance` scene serialization. Instantiate a character from one
   definition and verify that a reopened scene resolves the same asset.
4. **Possession seam.** Replace the temporary editor-only possession path
   with a single shared possession service used by editor play, standalone
   game client, and later server. Preserve the current working Jolt
   character-controller behavior.
5. **Camera Director.** Implement first-person and follow modes first,
   including collision and per-camera visibility; then top-down, detachable
   fly/drone, cinematic, and VR modes as additive directors.
6. **Locomotion behavior.** Move locomotion choice out of the special editor
   helper into a reusable POD contract. Native code provides safe character
   motion nodes; a graph chooses walk/run/jump/fall/idle and animation
   crossfades from resolved motion state.
7. **Blender compatibility.** Implement canonical-rig validation in the
   bridge, inspect/retarget the MakeHuman specimen, then generate/import a
   controlled animation set.
8. **Wardrobe proof.** Add the modular clothing/equipment asset recipe,
   per-camera body masking, and one stress-tested outfit before pursuing cloth
   simulation or procedural body generation.
9. **Authority and tooling.** Add per-game permission policies, camera mode
   allowlists, debug/recording controls, and live inspection for possession,
   camera, and locomotion state.

## Verification Gates

* Opening the same project in editor and game client resolves the same
  Character Definition and placed instance identity.
* First-person, follow, and top-down transitions preserve position,
  velocity, inventory, active behavior state, and animation playback.
* A detached camera returns to its subject predictably and cannot bypass a
  game's permission/range policy.
* No camera can see geometry marked hidden for itself while mirrors and other
  cameras continue to show the complete character correctly.
* A Blender export fails with actionable validation errors for bad scale,
  missing anchors, incompatible rig id, or empty animation curves.
* The MakeHuman specimen, canonical robot, and one clothed character pass the
  same measured-scale, animation, and physics-placement tests.
* Every new saved asset and scene component round-trips through VFS version
  history without OS-path dependencies.

## Explicit Non-Goals for the First Slice

No network prediction, full ragdoll, cloth simulation, procedural mesh
regeneration, facial performance capture, universal retargeting, or AI
behavior authoring is required before one possessed, clothed character can
move through a scene under multiple camera modes. Those features build on
this boundary; they do not define it.
