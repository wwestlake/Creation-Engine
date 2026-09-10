# Character Manufacturing System Specification

## Intent

The Character Manufacturing System produces reusable, game-ready character assets from a durable recipe rather than treating every exported mesh as a one-off. It supports artist-authored bodies, sculpt details, materials, clothing, rigs, animation sets, and gameplay behavior without requiring a monolithic character editor.

The system is technology agnostic at its boundaries. Blender, MakeHuman, custom Djehuti tools, and other importers may contribute source assets. FRust orchestrates evaluators, validation, and build graphs; it is not asked to replace a DCC application's vertex-editing tools.

This specification is original. MakeHuman is treated only as a reference for the general problem decomposition; no MakeHuman source code, file formats, or implementation is reused by this design.

## Primary Designer and Player Use Case

**As a game designer**, I can create reusable base character types -- human or non-human -- with a body, rig, textures, animation set, clothing catalog, and appearance controls. I can make named variations by adjusting age appearance, height, weight, muscularity, hair, materials, garments, and equipment, then publish those assets for the game to use.

**As a game designer**, I can configure a character-selection experience for a game. I choose which base character types are available, which appearance parameters and ranges a player may change, which hair/material/clothing choices are permitted, and how many player characters a profile may create.

**As a player**, I select one allowed base character type, customize only the appearance and clothing controls exposed by that game, and begin play with a saved character instance. If the game allows multiple characters, each instance has its own recipe overrides, clothing/equipment selections, identity, progression, and scene/world state while retaining the base type's compatibility contract.

### Acceptance Contract

1. A designer can publish a base character type without creating a player instance.
2. A designer can publish a named NPC or starting-character variation from the same base type without duplicating source meshes.
3. A game can list only allowed base types and only expose the designer-approved customization controls.
4. Player customization saves a compact instance recipe and references published assets; it does not write over the base type or artist source.
5. Multiple player-character slots can coexist in one game profile without sharing mutable inventory, progression, or appearance state.
6. Every accepted player variation passes the same rig, garment, material, physics-envelope, and animation compatibility validation as a designer-authored variation.

## Product Model

### Source Assets

Source assets preserve editability and artistic intent:

* Base body mesh with stable topology, UVs, bind pose, and canonical rig.
* Optional high-detail sculpt meshes used for baking, never required at runtime.
* Shape targets expressed as sparse vertex-position deltas against one base topology.
* PBR material sets: base color, normal, roughness, metallic, AO, opacity, mask, and optional displacement.
* Garment, hair, accessory, equipment, and prop meshes.
* Animation source clips and retarget profiles.
* Artist metadata: body family, coverage regions, garment slots, tags, licenses, quality level, and provenance.

### Recipe Asset

A `CharacterRecipe` is a small, editable data asset. It references source assets by stable VFS asset id and version policy; it never embeds operating-system paths or copies mesh bytes.

Its minimum fields are:

* Recipe id, schema version, name, description, tags, author, and provenance.
* Base body asset and canonical skeleton contract id.
* Named parameter values such as height, mass, muscularity, age impression, facial controls, and project-specific traits.
* Selected material variants and texture overrides.
* Ordered garment/equipment selections by slot.
* Locomotion, animation-set, capability, and behavior-Pod references.
* Build profile: preview, game-high, game-medium, game-low, or cinematic.
* Seed and variation policy for reproducible randomized NPC generation.

### Generated Character Definition

Evaluating a recipe emits a `CharacterDefinition`, the reusable game-facing asset described in `CHARACTER_AND_CAMERA_SYSTEM_PLAN.md`. It contains resolved references to build outputs, measured dimensions, collision settings, animation compatibility data, and the validation report that produced it.

The recipe remains editable. The generated definition is a versioned build product, comparable to a compiled material or imported model; it can always be rebuilt from its declared inputs.

## Parameter and Shape System

### Parameter Contract

Every controllable trait has a stable id, display label, value type, default, allowed range, semantic units, and optional UI grouping. Parameters must not be identified by slider position or a source-tool-specific name.

Supported kinds:

* Scalar range: `height`, `weight`, `muscle`, `nose_width`.
* Signed scalar: asymmetry or deliberate left/right offsets.
* Enum: body family, garment variation, hair style, material family.
* Boolean: freckles enabled, beard enabled, armor visor open.
* Color/material reference.
* Curve or profile: age distribution, muscle distribution, damage state.

The UI may expose artist-friendly labels and presets, while stored recipes use only stable parameter ids and values.

### Shape Targets

A shape target is a declarative asset:

```text
ShapeTarget {
  id
  baseTopologyId
  affectedVertexDeltas
  optionalNormalDeltas
  optionalRegionMask
  semanticTags
}
```

`affectedVertexDeltas` is sparse wherever possible. The evaluator combines the base position with the weighted sum of applicable target deltas. A target is valid only for its declared base topology; incompatible topology is a validation error, never a best-effort vertex-index guess.

### Dependency and Constraint Graph

Parameters do not merely map one slider to one target. A declarative graph describes direct mappings, derived values, coupled constraints, conflict resolution, and asymmetric overrides. Examples include height affecting limb and torso dimensions, normalized body-family weights summing to one, and garment size responding to body circumference.

The graph is pure dataflow with explicit inputs and outputs. It may be compiled to FRust for evaluation, but its persisted representation is data rather than opaque generated source. This permits inspection, validation, and tool independence.

## FRust Node Surface

Native code supplies safe host operations; FRust graphs decide what to assemble and when. Initial nodes should be narrow and observable:

* `character.recipe.create`, `character.recipe.read`, and `character.recipe.set_parameter`.
* `character.shape.evaluate` for a declared base body and parameter graph.
* `character.material.compose` for PBR set selection and baked-map overrides.
* `character.garment.fit` and `character.garment.attach`.
* `character.rig.validate` and `character.animation.validate`.
* `character.build.preview` and `character.build.publish`.
* `character.report.errors`, `character.report.warnings`, and `character.report.measurements`.

Host nodes return typed result values rather than throwing opaque errors into a graph. A failed rig check or garment fit returns a report with actionable asset ids and reasons. FRust algebraic result types are a natural fit for this surface once the language upgrade is live.

## Garment and Equipment System

### Garment Asset

A garment is modular, not a permanently fused copy of a body mesh. It declares:

* Slot: torso, legs, feet, head, hands, outerwear, back, belt, or custom.
* Compatible skeleton ids, bind-pose requirements, body families, and supported parameter envelope.
* Skinned mesh, materials, UVs, LODs, and optional cloth/physics regions.
* Coverage mask: body regions or material sections hidden under the garment.
* Layer order and collision/clearance hints for combined garments.
* Equipment anchors for rigid objects such as a sword, backpack, glasses, or tool.

### Fitting Modes

The first implementation supports three explicit modes:

* **Rigid anchor:** follows a named rig anchor with no skin deformation.
* **Shared-rig skinned mesh:** garment vertices use canonical skeleton bone weights. This is the default for shirts, trousers, gloves, and boots.
* **Surface-conformed mesh:** each garment vertex stores a compatible-body triangle, barycentric coordinates, and a normal-direction offset. The build evaluator follows the deformed body surface, then applies garment offsets and correction shapes.

Surface conformance is an authoring/build operation, not an unbounded per-frame mesh projection. Runtime animation remains normal skeletal skinning. Cloth simulation is later, and only on explicitly marked regions.

### Coverage and Clipping

Outfit coverage data controls body visibility per material or region. The master body is never destructively edited to suit one shirt. Per-camera visibility remains separate: a first-person camera may hide its own head while mirrors and other cameras retain it.

The build validator runs a standard pose stress set: neutral, arms raised, walk extremes, run extremes, crouch, jump, and seated pose. It records clearance/intersection warnings rather than promising perfect automatic cloth fit.

## Rig, Animation, and Camera Anchors

Each compatible character has one canonical skeleton id and a versioned anchor contract. Required anchors include root, pelvis, chest, head, left/right eyes, left/right hands, left/right feet, equipment sockets, and generic camera mounts. They may be bones or named attachment objects, but the contract records which.

The manufacturing build validates units, measured height, ground contact, object scale, bind pose, required bones/anchors, hierarchy, vertex-weight normalization, mesh-to-rig alignment, and real animation curve/duration data. The Camera Director consumes these anchors after animation and physics, so one character supports first-person, follow, top-down, detachable fly, cinematic, and VR modes without rebuilding.

## Build Pipeline

1. Resolve every recipe reference through VFS and freeze source versions for one reproducible build.
2. Validate base topology, skeleton contract, units, bind pose, UVs, and texture/material dependencies.
3. Evaluate the parameter/dependency graph into a body shape.
4. Fit selected garments and equipment, apply coverage/layer rules, and run pose stress validation.
5. Produce preview mesh and materials for Blender/Engine inspection.
6. Generate target LODs and bake high-detail information into runtime maps when a valid high-detail source is available.
7. Package resolved mesh hierarchy, rig, animation set, materials, measurements, and metadata as one versioned Character Definition.
8. Publish to VFS; scene instances reference the definition by asset id.

Every build reports inputs, versions, timings, warnings, measured height, vertex/triangle counts, texture sizes, LOD statistics, and the exact generated Character Definition id. Builds cache by a hash of the recipe plus resolved input versions.

## Blender and External Tool Integration

The Djehuti Blender bridge handles artist-facing work:

* Import a recipe preview or source body into a dedicated collection.
* Validate scale, topology identity, skeleton, anchors, weights, UVs, and materials before export.
* Generate or inspect shape targets, garment bindings, coverage regions, LODs, and baked maps with normal Blender workflows.
* Send source assets or generated preview results through the VFS bridge.
* Keep persistent Djehuti asset ids in Blender metadata for version updates.

MakeHuman is one possible upstream generator. Its editable source file and a rigged export enter as source assets; Djehuti does not depend on MakeHuman being installed, launched, or embedded in the distributed product.

## Quality and Safety Boundaries

* Never overwrite artist source meshes or textures. Generated results are new VFS versions or derived assets.
* Validation failures stop publishing but retain reports and preview data.
* Recipes identify expected source topology and skeleton.
* Randomized population generation records seed and variation values so any villager can be reproduced exactly.
* Runtime games consume validated published definitions, not arbitrary DCC source files.
* Expensive baking, retopology, and cloth simulation run as explicit jobs; they never silently occur in a frame tick.

## First Demonstration

1. Import one MakeHuman-derived, Blender-validated base body at canonical two-meter scale.
2. Publish a recipe with a small parameter set and canonical rig.
3. Add one shirt, trousers, footwear, and one rigid carried prop.
4. Build a Character Definition with Idle, Walk, Run, Jump, Fall, and Land clips.
5. Place it in a scene, possess it, and switch first-person, follow, top-down, and detached fly cameras without changing the character asset.
6. Save, reopen, and rebuild from VFS references to prove reproducibility.

Only after that demonstration passes should the project add automatic garment generation, broad body-morph compatibility, cloth simulation, crowd batches, facial capture, or full procedural population synthesis.
