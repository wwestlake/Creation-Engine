# Procedural Locomotion and Contact Model

## Purpose

Create believable, variable humanoid movement from open motion-reference
data, game intent, and world contact constraints. A motion clip is treated
as sampled multichannel signal data, not as a fixed visual sequence that
must be played unchanged. The system evaluates a desired pose, then uses
contact planning and inverse kinematics (IK) to make that pose fit the
actual ground, stairs, ladders, ledges, and moving supports.

This extends `ANIMATION_MODEL.md`; it does not introduce a separate
animation-programming system. FRust Pods remain the authorable behavior
layer. Native Engine code provides deterministic motion sampling, Jolt
queries, contact solving, and safe IK operations.

## Core Model

At simulation time `t`, the resolved skeletal pose is:

```text
finalPose(t) = IK(ContactConstrain(MotionField(intent, body, style, phase), world))
```

Where:

- `MotionField` selects and blends normalized reference motion according to
  intent and character parameters.
- `ContactConstrain` finds valid world-space targets and locks planted
  feet/hands to their supporting surfaces.
- `IK` adjusts legs, arms, pelvis, and torso to reach those targets within
  bone lengths and joint limits.

The signal source proposes a plausible human motion. World constraints have
final authority. A planted foot never slides merely because a source curve
continues moving it through the ground.

## Canonical Rig Contract

Procedural locomotion is available only to an asset that declares a valid
semantic joint-role map. Import remains generic for arbitrary armatures;
the role map is an authored compatibility layer used only by systems that
must address specific anatomy.

Required roles for the first humanoid profile:

- `root`, `pelvis`, `spine_lower`, `spine_upper`, `chest`, `head`
- `hip_l`, `knee_l`, `ankle_l`, `ball_l`, `toe_l`
- `hip_r`, `knee_r`, `ankle_r`, `ball_r`, `toe_r`
- `shoulder_l`, `elbow_l`, `wrist_l`
- `shoulder_r`, `elbow_r`, `wrist_r`

The profile also records left/right knee and elbow pole directions, foot
sole offsets, hand-grip offsets, joint limits, body measurements, and a
root forward/up convention. All geometry is authored and validated as one
unit equals one metre.

## Motion Records

Every source clip is retargeted to the canonical rig and converted into a
phase-normalized `MotionRecord`. Its duration stays available for source
provenance, but locomotion evaluation uses a cycle phase in `[0, 1)`.

```text
MotionRecord
  id, source license, source version, canonical rig id
  mode: idle | walk | run | sidestep | stair | climb | vault
  joint pose curves indexed by normalized phase
  root velocity and yaw curves
  left/right foot and hand contact intervals
  cadence, stride, turn, slope, and load ranges
  style tags: neutral, cautious, heavy, tired, stealthy, injured
```

Raw clips and their license records remain suite assets. Processed motion
records are derived assets with a reproducible derivation recipe so they
can be regenerated when retargeting or contact analysis improves.

## Motion Field Inputs

The first locomotion field accepts semantic values, never keyboard keys or
controller axes directly.

```text
LocomotionIntent
  desired planar velocity (m/s)
  desired facing direction
  desired yaw rate (rad/s)
  requested stance: stand | crouch
  requested gait: walk | run | sprint
  style modifiers: fatigue, load, caution, injury
```

Derived continuous variables include:

- `cadence`: steps per second.
- `strideLength`: intended distance between successive footprints.
- `stanceWidth`: lateral foot separation.
- `stepHeight`: swing-foot clearance.
- `weightShift`: pelvis displacement toward the support foot.
- `bodyLean`: forward/backward/turning torso lean.
- `armSwing`: amplitude and phase offset when hands are free.

The field evaluates neighboring motion records and blends their poses by
distance in this parameter space. Speed can therefore blend walk and jog;
turn rate can bias hips, torso, and feet toward compatible turning samples;
fatigue can shorten stride and reduce posture without a separate exhausted
clip for every possible speed.

## Gait and Root Motion

Phase advances on the fixed simulation schedule:

```text
phaseNext = wrap01(phase + cadence * deltaSeconds)
intendedSpeed = cadence * strideLength
```

The Jolt character controller remains authoritative for final root motion,
collision, gravity, and support state. The motion field supplies a desired
root displacement and pose; it never moves a character through a wall. The
difference between desired and achieved root displacement feeds stride
correction and prevents visual foot skating after collision.

## Contact State

Each potential contact has explicit persistent state:

```text
LimbContact
  limb role
  mode: free | approaching | planted | releasing
  target world position and normal
  supporting body or static surface identity
  local point on moving support
  acquisition phase and release phase
  reachability result
```

When a foot enters `planted`, its target remains fixed in the support's
local space. On a moving platform the target follows the platform. A swing
foot receives a predicted next target from desired velocity, stride length,
phase, collision sweep, and downward Jolt query.

## Surface Modes

`Ground` uses downward casts and surface normals. Foot pitch/roll follows
the support within ankle limits; slope shortens stride and changes pelvis
tilt.

`Stairs` identifies ordered tread surfaces. Each swing step targets a tread
instead of a generic projected ground point, with riser-clearance height,
shorter stride, and a distinct climb cadence.

`Ladder` has a named ladder component containing rung transforms and climb
direction. Hands and feet alternate between rung contacts. This is a
four-contact climb cycle, not walking with vertical velocity.

`Ledge`, `seat`, `rail`, tool grip, and vehicle grip reuse the same generic
contact contract with their own target providers.

## IK and Balance Order

Each fixed simulation tick runs this sequence:

1. Read resolved Jolt character motion and normalized intent.
2. Select and blend motion-record pose signals.
3. Advance gait or climb phase.
4. Acquire, maintain, or release contact targets with Jolt queries.
5. Solve legs and arms against locked targets.
6. Adjust pelvis height/translation and tilt to preserve reachability.
7. Apply torso counterbalance, head/gaze, and bounded procedural variation.
8. Enforce joint limits and emit the final skeletal pose for rendering.

The first solver is analytic two-bone IK for legs and arms. It must report
unreachable targets rather than silently stretching bones. A later full-body
solver may improve balance, but it does not replace this reliable baseline.

## Controlled Variation

Variation is deterministic and filtered, keyed by character instance id and
gait phase. It may affect stride asymmetry, arm swing, posture, head motion,
and timing inside declared bounds. It must never modify a planted contact
target or violate a joint limit.

This yields individual movement without per-frame random noise, replay
instability, or loss of contact fidelity.

## FRust Surface

The native layer owns math and safety. Pods select intent, style, actions,
and transitions through result-returning nodes:

- `motion.set_intent`
- `motion.set_style`
- `motion.request_surface_mode`
- `motion.get_state`
- `motion.get_contact`
- `motion.request_transition`

The nodes operate on an explicit character/puppet instance. They return
failure details when a requested action is unreachable, unsupported by the
rig, or has no valid world contact.

## First Vertical Slice

The first deliverable is deliberately narrow and testable:

1. One validated canonical humanoid rig with real walk, jog, idle, and turn
   reference clips.
2. A motion-record importer that phase-normalizes clips and marks left/right
   foot contacts for author review.
3. Flat ground, slopes, and stairs with planted-foot locking and analytic
   leg IK.
4. Jolt-authoritative root movement with visible contact/debug overlays.
5. Deterministic replay tests for pose, contact, and root-motion output.

Ladder and hand contacts begin only after planted-foot motion is stable.
Cloth, facial animation, network prediction, and general full-body IK are
later layers, not prerequisites for credible locomotion.

## Implementation Status

The first native foundation is implemented in
`Source/Animation/ProceduralLocomotion.{h,cpp}` and covered by
`CreationEngineProceduralLocomotionSmoke`. It deliberately has no renderer,
UI, Jolt, or input dependency. The module currently provides:

- capture-point/LIPM footstep proposals;
- eased swing trajectories with clearance;
- support-local contact anchors for moving platforms;
- analytic no-stretch two-bone target solving with pole control and explicit
  pelvis-correction reporting;
- quaternion foot-up to ground-normal alignment.

The next integration phase will add the validated humanoid joint-role map,
Jolt sweep/raycast target acquisition, and application of the solver's
targets to sampled local joint transforms. Jolt remains the authority for
root movement and collision throughout.

## Acceptance Criteria

- At least two walks and one jog blend smoothly by speed and cadence.
- Feet remain locked while planted on flat terrain, slopes, stairs, and a
  translating platform.
- The character never stretches a leg beyond its configured reach.
- A character facing or moving transition does not create a 90-degree root
  convention error; the profile validation rejects inconsistent axes.
- The same input, motion library version, world state, and character seed
  produce the same pose and contacts in deterministic replay.
- A Pod can request a walk, turn, stop, and stair climb without containing
  raw joint math or direct keyboard/controller knowledge.
