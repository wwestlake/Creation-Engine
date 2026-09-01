# Creation Engine Asset Management Plan

## Purpose

Creation Engine is a product authoring environment. Its scenes, objects,
materials, textures, behaviors, and bundled content must be authored data in
the suite VFS. They must never be temporary renderer state or C++ code that
creates content at startup.

This plan defines the complete Engine asset-management system. It uses the
suite's existing VFS service, project sessions, stable asset IDs, versions,
catalogs, and materialization services. It does not redesign asset packaging
for every Creation application.

## Product Rules

1. **A pack is a complete content unit.** It may contain any Engine asset:
   scenes, object definitions, models, meshes, skeletons, animations,
   textures, materials, material graphs, shaders, audio, UI, FRust source,
   compiled/generated outputs, presets, metadata, and dependencies.
2. **The suite asset library is canonical.** A pack is installed once in the
   suite VFS and has a stable pack ID plus immutable versions.
3. **A project owns its authored work.** A game stores its scenes and
   project-created assets in its project VFS area. It records exact pack
   versions it uses rather than copying an installed pack by default.
4. **References are stable.** Scene and object references identify a pack,
   pack version, asset ID, and asset version. Display names and VFS paths are
   discovery information, never authority.
5. **The Engine Pack ships with the product.** It is substantial starter
   content, not a token demo: Default Scene, primitives, starter materials,
   textures, examples, behaviors, and useful authoring content are all data
   in this pack.
6. **No C++ generates scene content.** Engine code loads, resolves, imports,
   saves, and renders assets. It does not create floors, walls, cubes, or any
   other authored scene contents.

## Asset Pack Contract

Each installed pack has a VFS root addressed by `packId` and `packVersion`.
Its manifest is the entry point and declares:

- pack ID, version, name, publisher/creator, description, licensing/store
  metadata, supported Engine and suite versions, and content hash;
- declared dependencies on exact pack versions;
- every asset record: asset ID/version, type, display name, tags, logical
  payload path, content hash, provenance, and metadata;
- asset-to-asset dependencies, including material-to-texture,
  object-to-model/material/behavior, scene-to-object/material/behavior, and
  FRust-to-required-domain dependencies;
- optional thumbnails, documentation, examples, and editor presentation
  metadata.

The payload layout is pack-owned and may have folders such as `Scenes/`,
`Objects/`, `Models/`, `Textures/`, `Materials/`, `Audio/`, `Code/`, and
`Metadata/`. Folder names are organizational only; the manifest and stable
IDs are authoritative.

The shared asset model gains the generic asset types needed by Engine content:
scene, object definition, model/mesh, texture, material, material graph,
skeleton, animation, UI, and asset pack. Engine-specific classification is
stored in category/tags/metadata rather than inventing a private parallel
catalog.

## Storage and Resolution

### Suite asset library

The suite VFS service owns the installed-pack library. It provides discovery,
read, installation, validation, removal, and change notification. Engine
never reaches into an OS folder to find a pack.

The shipped Creation Engine Pack is installed by suite/Engine setup exactly
like any other pack, with trusted product provenance. Store downloads and
creator-supplied packs use the same installer and manifest validation path.

### Game project VFS

Each Engine game has VFS documents for:

- game metadata and the game's required pack set;
- editable scene assets;
- project content pack(s) holding assets authored or imported for that
  project;
- game-specific FRust and generated artifacts;
- build/export metadata.

The game catalog records the active game, its entry scene, and required packs.
The game may use exact asset versions only by default. An explicit future
upgrade command can update a game to newer compatible pack versions after
showing the affected references.

### Resolver

Creation Engine has one asset resolver used by the viewport, hierarchy,
inspector, material editor, FRust host, runtime client, and eventual export
pipeline. Given an asset reference, it:

1. checks the active game's project content pack;
2. checks the exact installed pack/version declared by the game;
3. reads the descriptor and payload through the VFS service;
4. materializes only when a third-party decoder requires a real temporary
   file; and
5. returns a typed, cached Engine resource or a visible diagnostic.

There is no fallback to arbitrary renderer names, loose files, or invisible
in-memory catalogs. A missing dependency is shown as a named missing asset in
the editor and prevents a runtime/export validation from reporting success.

## Scenes, Objects, and the Default Scene

A scene is a normal Engine asset. Its serialized document contains entity
identity, hierarchy, transforms, object references, material references,
behavior references, scene settings, and editor metadata. It does not embed
GPU pointers or machine paths.

The **Default Scene** is an authored scene asset in the shipped Creation
Engine Pack. It is the starting template for every newly created Engine scene.

New Scene behavior is fixed:

1. resolve the Default Scene from the installed Creation Engine Pack;
2. copy its scene data into a new editable scene asset in the active game's
   VFS area;
3. retain its asset references to the Engine Pack's exact declared versions;
4. open the new scene and set it as active; and
5. save subsequent edits only to the game scene.

The Default Scene itself is never silently modified by a game. Later, users
can create their own template packs and select a template deliberately; that
extends this same mechanism rather than introducing a second scene system.

Object definitions/prefabs are also normal assets. They package composition,
defaults, component data, material slots, behavior attachments, and dependent
assets. Placing an object creates a scene entity referencing the object asset;
it does not duplicate renderer-only state into C++.

## Imported and Authored Content

The Engine Import panel becomes a VFS authoring surface.

- Importing a source file creates a managed asset record in the active
  project's content pack, records source provenance and import settings, and
  stores source/derived payloads through the shared project asset service.
- A model import creates managed model, mesh, skeleton, animation, material,
  and texture assets as applicable, preserving their relationships in the
  manifest.
- Texture import creates a texture asset. Material authoring creates material
  or material-graph assets that reference texture assets by ID/version.
- FRust authored for an object or game becomes a managed code asset with its
  domain/capability manifest and dependencies.
- The viewport catalog is a cache of resolved VFS assets. It is rebuilt from
  pack/project catalogs at open time and updated after imports; it is never
  the place an import stops.

An external creator uses the same manifest and payload contract to build a
pack. The Engine offers **Install Asset Pack** and validates schema, hashes,
dependencies, Engine compatibility, and duplicate pack/version identity before
making it available. The pack remains in the central suite library; a game
adds it deliberately as a required dependency.

## Engine User Interface

The Engine requires domain-specific authoring surfaces in addition to the
suite's general asset browser:

- **Asset Packs:** installed packs, versions, publisher/license, dependency
  graph, contents, validation status, install/remove/update actions.
- **Game Content:** the active game's required packs, project-created content,
  entry scene, and dependency health.
- **Scene Browser:** scenes in the active game, create from Default Scene,
  open/save/rename/delete, and entry-scene selection.
- **Object Browser:** objects/prefabs available to the active game, searchable
  by type/tag/pack, with placement and dependency information.
- **Material and Texture Browser:** textures, material instances, material
  graphs, slots, previews, and source/derived provenance.
- **Import:** direct files and complete packs; every successful result is
  visible immediately in the appropriate browser.

These are product workflows, not debug views of raw VFS entries.

## Runtime, Validation, and Export

The editor and game client use the same resolver and game dependency list.
Opening a runtime client validates that every referenced asset and dependency
is resolvable before launch.

The future game export step takes the same resolved dependency graph and
creates a self-contained game content bundle. That export copies the exact
asset versions required by the game; it does not require games to duplicate
all installed pack content during normal editing.

## Temporary Paths to Remove

- viewport-created starter rooms and any C++ scene/content generator;
- private Engine-only packaged assets that bypass the suite pack manifest;
- renderer-name references as saved scene authority;
- Import-panel assets that exist only in the live catalog;
- duplicate scene/project storage paths outside the VFS service.

## Implementation Program

### Delivery checklist

- [-] Engine Pack source, manifest, and VFS installer implemented. Generic external-pack installation contract added; runtime verification remains.
- [x] Authored Default Scene added to the shipped Creation Engine Pack.
- [-] New game and New Scene copy Default Scene into the active game VFS; awaiting build/test.
- [-] Game metadata records the exact required Creation Engine Pack version; awaiting build/test.
- [-] Scene and object definitions now persist asset ID/revision plus pack ID/version. Opening a scene loads every referenced VFS pack before resolving project-owned imports; renderer cache keys are pack-qualified to prevent collisions.
- [ ] One VFS-backed resolver replaces the viewport's private catalog path.
- [-] Imports store source bundles (entry file plus decoder dependencies) as managed project content before live preview; resolver refresh remains.
- [ ] Pack installation validates manifests, content hashes, and dependencies.
- [ ] Asset Packs, Game Content, Scene, Object, Material, and Import product UI.
- [ ] Runtime/export dependency validation.

### 1. Shared contract completion

Extend the existing shared asset descriptors/catalog and VFS service with
pack identity, pack manifests, installed-pack discovery, immutable versions,
dependency validation, and notifications. Reuse `ProjectSession`,
`ProjectAssetService`, and the existing asset ID/version rules; do not create
an Engine-private storage layer.

### 2. Creation Engine Pack and template data

Author the shipped Creation Engine Pack as VFS package data. Move bundled
primitives and existing package payloads into its manifest. Author Default
Scene as serialized scene data inside the pack. Remove all Engine code that
creates authored entities.

### 3. Pack-aware game and scene documents

Extend the Engine game document schema with required pack references. Change
scene/object/material serialization from catalog names/GPU pointers to stable
asset references. Implement New Scene from Default Scene and make open/save
operate only on project VFS documents.

### 4. Resolver and viewport integration

Replace the private viewport catalog load path with the common resolver.
Resolve scene assets lazily into GPU resources through the VFS/materialization
path. Surface missing dependencies in the hierarchy, inspector, viewport, and
runtime validation.

### 5. Import and authoring integration

Connect all importers to project content packs and the shared asset service.
Add the pack installer/validator. Make material and texture authoring create
the same managed assets and dependency records.

### 6. Product UI and game export dependency validation

Build the Engine pack, game-content, scene, object, material, and import
surfaces. Add runtime and export dependency validation using the shared
resolver.

## Verification and Acceptance

The system is not complete until all of the following pass:

1. A clean suite install has the Creation Engine Pack installed and visible.
2. Creating a project/game creates no scene entity in C++; New Scene creates
   an editable VFS copy of Default Scene.
3. Default Scene opens with its distinct models, textures, materials,
   behaviors, and colors; save/close/reopen preserves the result exactly.
4. An imported model with textures and animations becomes managed project
   content, survives restart, and can be placed and saved in a scene.
5. A creator-supplied pack validates, installs once, appears in the Engine
   pack browser, and can be added as a game dependency.
6. A scene containing Engine Pack and external-pack content reports every
   exact dependency, opens in the game client, and fails visibly—not
   silently—if one dependency is missing.
7. Texture and material assets can be created, changed, attached to objects,
   saved, reopened, and resolved by the game client through the same system.

## Explicit Boundaries

This plan completes Creation Engine's use of the shared suite asset/VFS
infrastructure. It does not require immediate implementation of the
LagDaemon store, payment, download entitlement, cross-machine sync, or every
other app's domain UI. Those systems consume this pack contract later; they do
not change the Engine's content model.
