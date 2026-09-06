# Djehuti Bridge Protocol

## Scope

This document specifies the engine/service-side protocol for getting
content from an external authoring tool into Djehuti Engine (this repo,
`apps/CreationEngine` -- historically named "Creation Engine" in code
paths; that name is being retired for the product, the code paths are
unchanged). It is deliberately **tool-agnostic**: it does not cover
Blender, GIMP, Reaper, or any other specific tool's own UI, menu
placement, or native-format quirks. Building each tool's own plugin
against this protocol is a separate concern.

**In scope**: how an external process discovers the running suite,
finds a project, learns what the engine currently expects (formats,
conventions), sends a file in, and finds out what happened.

**Out of scope, deliberately, for now**: opening/editing an existing
engine asset back inside the external tool (round-trip). This protocol
only covers content flowing IN. Round-trip is a real, separate future
piece -- named here, not designed.

## Everything reuses existing, already-built mechanism

Nothing in this protocol requires a new HTTP endpoint or a new engine
process. It rides entirely on:
- `services/VfsService` -- the suite's existing standalone process,
  already running HTTP + WebSocket on localhost, already used by every
  suite app for project storage.
- The engine's existing `ImporterRegistry`/`AssetImporter` pipeline
  (`apps/CreationEngine/Source/Import/`) -- the same code path drag-and-
  drop import already uses.
- `DjehutiImportWatcher` (`apps/CreationEngine/Source/Import/
  DjehutiImportWatcher.h/.cpp`) -- already built, already wired into
  `MainComponent`, already watches the current project's VFS `Imports/`
  folder and runs a dropped file through that same importer pipeline.

## 1. Discover the suite VFS service

Read `%APPDATA%\Creation Suite\suite-settings.json`
(`creation::suite::SuiteSettings`, `shared/AssetSystem`). Relevant
fields:
- `suiteVfsRoot` -- where VFS data actually lives on disk. Not needed by
  an external tool directly (everything goes through the HTTP API
  below), but present for completeness.
- `suiteExecutablesRoot` -- directory containing the suite's built
  executables, including `CreationSuiteVfsService.exe`. Used only if
  the service isn't already running (step 2).

Enumerate `%APPDATA%\Creation Suite\processes\*.json`
(`creation::services::SuiteProcessRegistry`). Each file is one live
suite process's registration record:
```json
{
  "appId": "CreationSuiteVfsService",
  "processId": 12345,
  "httpPort": 54321,
  "lastHeartbeatMs": 1234567890123,
  ...
}
```
A record with `appId == "CreationSuiteVfsService"` and a
`lastHeartbeatMs` within the last ~15 seconds is a live instance --
its `httpPort` is the base for every HTTP call below
(`http://127.0.0.1:<httpPort>/...`).

## 2. Launch the service if it isn't running

If no live record is found: launch
`<suiteExecutablesRoot>\CreationSuiteVfsService.exe` and poll
`GET /health` (expects `{"status":"ok"}`) until it responds, with a
reasonable timeout (a few seconds). This mirrors
`SuiteVfsServiceClient::discover()`'s own behavior exactly.

## 3. List projects

`GET /project/list` returns every project in the suite (there is no
per-app project ownership -- see
`docs/architecture/Suite-Shared-Project-Model.md`; any project may
already contain content from other suite apps):
```json
[
  { "projectId": "...", "manifest": { "projectName": "...", ... }, "totalSizeBytes": 12345 }
]
```
Let the user (in whatever UI the tool provides) pick one. There is no
"create a project" responsibility for this protocol -- assume one
already exists, created through the engine itself.

## 4. Read the export contract

Two reads, both via the VFS service's existing generic entry API:

- **Global default**: `GET /suite/entry?path=djehuti-export-contract.json`.
  The engine guarantees this exists (writes a default on first run if
  absent) -- see `creation::assets::EnsureDjehutiExportContractDefaultExists`,
  `shared/AssetSystem/include/creation/assets/DjehutiExportContract.h`.
- **Per-project override**, if present: `GET /project/entry?projectId=
  <id>&path=Metadata/djehuti-export-contract.json`. Only some projects
  will have one; absence is normal, fall back to the global default.

Shape:
```json
{
  "acceptedFormats": ["gltf", "glb"],
  "upAxis": "+Y",
  "unitsPerMeter": 1.0
}
```
- `acceptedFormats` is authoritative and enforced by the engine (see
  step 6) -- do not send a format not on this list, it will be rejected.
- `upAxis`/`unitsPerMeter` are advisory conventions for whatever the
  tool is exporting spatial/geometric content (3D meshes, primarily).
  They are informational only: the engine does not validate or reject
  on a mismatch here. Format-specific note: for glTF specifically,
  `+Y`/`1.0` are also glTF's own spec defaults, so a standard glTF
  exporter already produces the right thing without extra
  configuration. A tool exporting non-geometric content (an image, an
  audio file) has no use for these two fields.
- **Current implementation status**: `acceptedFormats` today is
  `["gltf", "glb"]` only, hard-gated in `DjehutiImportWatcher`. Image
  and audio *importers already exist* in the engine
  (`ImporterRegistry::RegisterBuiltins`, already handling
  `.wav`/`.aiff`/`.flac`/`.png`/`.jpg`/`.tga`/`.bmp`/`.hdr` via
  `ImportPanel`'s own drag-and-drop today) -- widening the watcher's
  format gate to match is a small, separate follow-up, not yet done.
  Read `acceptedFormats` from the contract at request time rather than
  hardcoding an assumption, so a tool automatically picks up that
  widening once it ships, with no changes on the tool side.

## 5. Stable asset ID

Every asset the engine tracks has a durable id (`"asset:" + uuid`,
`creation::assets::AssetDescriptor::id`) that survives re-import
(`ProjectAssetService::createNewVersion`/`AssetImporter::Reimport`) --
this is what makes "send again" update the existing asset instead of
creating a duplicate.

- **First send of a given piece of content**: no id to send. The
  engine mints one and returns it (step 7) -- store it in whatever
  the tool's own persistent per-object metadata mechanism is (a custom
  property, a sidecar, however that tool keeps state).
- **Subsequent sends of the same content**: send the previously-
  returned id back, embedded in the file itself, so the engine can find
  the existing asset without a separate lookup call.
- **Current implementation status, per format**: today, only glTF/GLB
  is understood, via `asset.extras.djehuti_asset_id` -- a plain string
  key in glTF's own standard, spec-legal `extras` mechanism (see
  `GltfLoader.h`'s `LoadedModel::djehutiAssetId`). No equivalent exists
  yet for image or audio formats (both have their own metadata-
  embedding conventions -- PNG text chunks, WAV `LIST`/`bext` chunks --
  but the engine does not currently read any of them). Until that's
  built, a resend of a non-glTF format will always be treated as a
  brand-new asset (no dedup) -- a real, known gap, not a silent
  limitation.

## 6. Send the file

`PUT /project/entry?projectId=<id>&path=Imports/<name>.<ext>`, raw file
bytes as the request body, `Content-Type: application/octet-stream`.

- `<ext>` must be one of `acceptedFormats` (step 4) -- anything else is
  rejected (see step 7's error result).
- `<name>` should be stable/meaningful (used verbatim in the result
  path, step 7) but has no other required structure.
- This must be a single, complete PUT with the whole file in one
  request body -- the watcher reacts to the write-completed event, not
  a partial/streamed one, so there's no "wait for the rest of the
  bytes" step to design around; one full PUT is naturally atomic from
  the watcher's point of view.

## 7. Read the result

The engine's watcher is listening for exactly this write, on the
project's VFS entryChanged broadcast, and reacts automatically (nothing
else to trigger). Once it has processed the file, it writes:

`Imports/.results/<name>.<ext>.json` (note: the full original filename,
including its extension, plus `.json` appended -- not the name with the
extension stripped):
```json
{ "ok": true, "assetId": "asset:1b2c3d4e-..." }
```
or
```json
{ "ok": false, "error": "human-readable reason" }
```

Poll `GET /project/entry?projectId=<id>&path=Imports/.results/<name>.<ext>.json`
for a few seconds until it appears (a 404/empty response means "not
processed yet," not failure -- keep polling up to a reasonable timeout,
a few seconds is normally enough). On success, store `assetId` back
into the tool's own per-object stable-id storage (step 5).

## Everything not covered here

Anything about how a specific tool's own plugin is built -- its menu
items, its own settings UI, how it walks its own scene graph, how it
decides what to export, how it embeds `djehuti_asset_id` into whatever
format it's exporting -- is that tool's own concern, not part of this
protocol.
