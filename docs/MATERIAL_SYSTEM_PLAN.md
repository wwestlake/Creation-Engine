# Material System — Plan

**Status update:** Stages 1–3 below are DONE and tested — the "Materials"
dock panel is the real node-graph editor described in
`Source/Views/MaterialGraphPanel.h` (not the `PlaceholderPanel` this doc
originally described), backed by the real `CompileMaterialGraph` pipeline,
with named/reusable Material assets via `AssetCatalog::GetOrCreateMaterial`.
Stage 4 (copy-on-write / per-instance material detach) is still open --
confirmed directly against `MaterialGraphPanel.h`'s own current header
comment: "Compile saves the compiled shader+parameters onto the named
material's live Material object (**which every mesh slot referencing it
shares**)." The shared-Material mutation behavior this doc originally
flagged is real and current, not stale -- only the "what's built" framing
below was stale. Fix your understanding accordingly before reading Stage 4
as hypothetical.

## The core principle

A material *is* a node graph. There is no separate "simple material" tier
that bypasses the graph system — even a flat color is just a trivial graph
(a constant-color node into Surface Output), not a special case. Texture
support is not a slot on a struct; it's whatever `material.texture.sample2d`
nodes a given graph happens to contain — as many or as few as the graph
actually uses. This mirrors Unreal's real architecture (confirmed against
actual UE source, not assumed): `HLSLMaterialTranslator` compiles a node
graph into HLSL, spliced into a shared host template (`MaterialTemplate.ush`)
that owns lighting; the graph never owns lighting itself.

## What already existed, real and working, before tonight

Checked directly against the code, not assumed:

- `shared/NodeSystem` — the graph/node-type-registry infrastructure, already
  proven working (this is what the FRust Logic panel's real node editor
  already runs on).
- `shared/MaterialSystem` — a genuine, complete compiler
  (`CompileMaterialGraph`) that walks a graph and emits real GLSL: a
  `declarations` block (uniforms for any parameter/texture nodes used) and
  an `evaluateFunction` — a complete function,
  `void EvaluateMaterial(in vec2 vUV, out vec3 baseColor, out float metallic, out float roughness)`.
  Real node types are already registered: UV, constant float/color,
  parameter float/color, math ops, texture sample, surface output.
- `ce::Material` (`Source/Render/Scene/Material.h`) — a genuinely
  well-designed variant-resolution class, not a placeholder. It already
  handles shader variant switching (`USE_ALBEDO_TEXTURE`, `USE_SKINNING`)
  through `ShaderComposer`, with proper caching. This class is being
  *extended*, not replaced.
- `ShaderComposer` — a real `#include`/`#define` composition layer over
  `juce::OpenGLShaderProgram`, with disk and VFS source modes and program
  caching already built.

## What was placeholder, and is being replaced rather than patched

- `pbr_lit.frag` — a fixed, hand-written shader with hardcoded
  `uAlbedo`/`uMetallic`/`uRoughness` uniforms and exactly one hardcoded
  optional texture slot (`USE_ALBEDO_TEXTURE`). Built for "an imported
  glTF happened to have a diffuse texture," not as a general material
  system. This is *not* kept as a permanent "simple" tier alongside the
  real graph system — every material becomes a graph, full stop.
- The "Materials" dock panel — was a literal `PlaceholderPanel` ("Node-based
  material editor - coming soon"), zero wiring to any of the above. **Since
  replaced** by the real `MaterialGraphPanel` (Stage 2, done -- see status
  note at top of this doc).

## Progress made tonight (uncommitted, same branch as everything else)

1. `ShaderComposer` extended with a `materialSource` parameter and a
   `//$MATERIAL_SOURCE$` marker-substitution mechanism — the injection
   point a compiled graph's `declarations` + `evaluateFunction` fills.
   Content-hashed into the program cache key, since it's per-call
   generated text, not a file path.
2. `programs/material_host.frag` created — the lighting/tonemap code from
   `pbr_lit.frag`, kept (it's real, correct code, not placeholder), with
   the fixed material inputs replaced by a call to `EvaluateMaterial(...)`
   at the injection point.

## What's left, staged

**Stage 1 — prove the pipeline end-to-end — DONE**
Extend `ce::Material` with an optional compiled-graph path (when present,
`Resolve()` requests `material_host.frag` + the compiled source instead of
the fixed `pbr_lit` pair; `ApplyUniforms()` skips the fixed uniforms for
that path). Build one hand-authored test graph in code, compile it, apply
it to one real object, verify it actually renders — visually confirmed,
not just "should work."

**Stage 2 — the real node-editor UI — DONE**
Replace the Materials placeholder panel, reusing the same
`NodeGraphComponent`/`NodePalette`/`NodeInspector` machinery already proven
in the FRust Logic panel, pointed at the material node registry instead.
Texture assignment falls out of this for free — dragging in a Texture
Sample node *is* how you add a texture, matching how it actually works in
UE4 (confirmed against the user's own working knowledge of it, not just the
code).

**Stage 3 — named, reusable Material assets — PARTIAL**
"New Material" name/metadata dialog → its own dedicated diagram window,
saved as a real VFS-backed asset referenced by objects — not embedded
per-object, so the same material can be shared and edited once. Named/
reusable at the runtime `AssetCatalog` level is done -- confirmed via
`MaterialGraphPanel.h`'s own header comment. Durable VFS persistence of
the actual graph (node layout/wiring, not just its compiled shader output)
is NOT done yet -- same comment states this panel "does not yet persist/
reload a DIFFERENT named material's own graph... would need each
material's graph serialized via .frgraph and reloaded on open." Don't
treat Stage 3 as fully closed without checking that specific gap first.

**Stage 4 — copy-on-write for materials**
Same asset/instance/detach principle designed for vertex edit mode
(`VERTEX_EDIT_MODE_PLAN.md`) applies here too: editing a material in place
on one object must not silently repaint every other object sharing that
asset (the still-unfixed `MaterialsPanel` shared-Material bug from earlier
this session is exactly this failure, already diagnosed, not yet fixed).
Not built in this pass, but the mechanism from vertex editing's detach
model should extend here rather than being reinvented.

## Explicitly out of scope for now

- Multiple simultaneous textures / arbitrary node counts aren't a hard
  problem to solve later — the compiler already supports it structurally
  (each texture-sample node in the graph gets its own uniform + sample
  call). Nothing here artificially limits it; it's just not exercised by
  the Stage 1 test graph.
- A full shading-model system (multiple BRDFs, transparency modes,
  subsurface, etc.) — the current host template does one PBR path, same as
  today. Real follow-up territory, not part of closing today's gaps.
