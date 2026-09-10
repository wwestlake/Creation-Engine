# Character Creator Implementation Plan

## Goal

Build an Engine-hosted character creator where a game designer publishes reusable human or non-human base character types, configures player-facing customization, and makes finished characters available to a game. Players select an allowed base, change only approved appearance and wardrobe controls, save one or more independent characters, and begin play as the selected character.

The first proof is Dave: a MakeHuman-derived male base character, validated in Blender, with a small body-morph set, hair/material options, shirt, trousers, boots, and a usable animation set.

## Core Assets

* **Base Character Type:** designer-owned family with body, topology id, canonical rig, shape-target library, wardrobe catalog, animation set, and defaults.
* **Character Recipe:** editable choices layered on a base type: morph values, appearance, garments, equipment, and behavior references.
* **Character Definition:** immutable, validated build output resolved from one recipe and its frozen input versions.
* **Character Instance:** one player/NPC identity with its own recipe overrides, equipment, progression, inventory, and world state.
* **Character Creator Policy:** designer-authored permission envelope: allowed bases, exposed parameter ranges, wardrobe/material options, and maximum player-character slots.
* **Character Roster:** a profile's character instances and active selection.

## Phase 0: Shared Contracts

1. Create the shared `CharacterSystem` CMake target and unit tests for `CharacterManufacturingMath.h`.
2. Define versioned schemas for every core asset above.
3. Add one suite-level character asset classification; use category/media metadata for base types, recipes, garments, and definitions rather than making `AssetKind` app-specific.
4. Define canonical metadata: meters, Y-up converted coordinates, ground convention, topology id, skeleton id/version, anchor contract, and asset-reference policy.
5. Add VFS save/list/reopen/version/validation tests before UI work.

**Exit:** a project can persist and resolve all character data without OS paths.

## Phase 1: Dave Intake

1. Preserve Dave's MakeHuman file as editable upstream source.
2. Export rigged Dave to Blender, then import through the Djehuti bridge into VFS.
3. Validate and report measured height, ground offset, scale, UVs, vertex count, skeleton hierarchy, anchors, and skin weights.
4. Normalize Dave in Blender to canonical two-meter scale and ground contact; do not silently scale him in Engine.
5. Publish the validated mesh, materials, rig, and report as the first Base Character Type.

**Exit:** Dave loads from VFS at correct scale and ground placement with stable identity and a valid skeleton.

## Phase 2: Morphs and Recipes

1. Start with apparent age, height, weight, muscularity, skin material, hair style, and hair color.
2. Generate controlled Dave variants with identical topology, calculate sparse target deltas, and store parameter mapping/ranges as versioned assets.
3. Build a declarative dependency graph from artist-friendly controls to target weights and derived values such as capsule dimensions.
4. Evaluate an explicit preview/build job. The first version may rebuild preview geometry on change; GPU runtime morph animation is not required.
5. Publish a deterministic Character Definition from the recipe and resolved inputs.

**Exit:** the same recipe and source versions produce the same character definition; supported sliders visibly change Dave.

## Phase 3: Wardrobe

1. Define garment metadata: slot, skeleton/body compatibility, coverage mask, layer order, materials, LODs, and fit mode.
2. Build Dave's shirt, trousers, boots, and one rigid carried prop in Blender.
3. Validate shared-rig weights under neutral, walk, run, jump, crouch, arm-raise, and seated pose stress tests.
4. Implement shared-rig skinned garments and rigid-anchor equipment first; surface-conformed fitting follows after the simple wardrobe is proven.
5. Add coverage masks so skin does not poke through clothing.

**Exit:** an independently assembled outfit saves, reopens, animates, and passes the pose test set.

## Phase 4: Designer Tool

1. Add a Character Creator panel to Djehuti Engine backed only by VFS assets and build jobs.
2. Let designers create/open bases and recipes, inspect reports, adjust exposed parameters, select garments, and preview in the viewport.
3. Publish definitions without overwriting source art or base types.
4. Add Character Creator Policy editing: allowed bases, controls/ranges, permitted catalogs, and roster slot count.
5. Show previewing, validating, published, stale-input, warning, and blocking-error state clearly.

**Exit:** a designer can publish Dave plus two named variations and configure a game policy without editing files or code.

## Phase 5: Player Creator and Roster

1. Add game-facing selection/creation that reads Character Creator Policy.
2. Display only approved base types, controls, ranges, and wardrobe choices.
3. Save player recipe overrides as a Character Instance; never mutate the designer base.
4. Support multiple independent slots when policy permits, with separate appearance, equipment, inventory, progression, and world-state references.
5. Add select, rename, delete-with-confirmation, and active-character actions.

**Exit:** a player creates two Dave-derived characters, reopens the project, and starts play as either without state leaking between them.

## Phase 6: Runtime

1. Replace the temporary editor possession path with the shared Character Instance/Possession service.
2. Add Puppet Runtime for normalized intent, Jolt motion, animation state, interaction anchors, and lifecycle events.
3. Add Camera Director modes in order: first person, follow, top down, detached fly/drone, cinematic, then VR.
4. Move locomotion selection into a reusable FRust POD driven by resolved motion state.
5. Keep camera permissions separate from character assets.

**Exit:** one published Dave variation is possessable through first-person, follow, top-down, and detached cameras without losing state or changing its definition.

## Phase 7: FRust and Automation

1. Expose typed nodes for recipe mutation, shape evaluation, garment selection/fitting, validation, preview, publish, and reports.
2. Use FRust algebraic result types for build outcomes.
3. Allow designer PODs and optional AI helpers to generate valid NPC variations only within policy limits and through the same validation/publish path.

## Test Matrix

* VFS schema/version round trips and deterministic build hashes.
* Invalid topology, anchors, units, UVs, or weights report actionable errors.
* Garment compatibility and pose stress validation.
* Player policy rejects hidden/disallowed controls even for altered saved requests.
* Multi-character roster isolation.
* Camera transitions preserve puppet position, velocity, inventory, behavior, and animation state.

## Deferrals

Automatic retopology, universal retargeting, cloth simulation, facial capture, arbitrary runtime mesh mutation, crowd synthesis, network prediction, and automatic AI art generation are all later work. They are not required for Dave's complete designer-to-player loop.
