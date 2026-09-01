# Vertex Edit Mode — Plan

Object-mode manipulation (move/scale/rotate a whole placed object, built and
verified working this session) is one level above what this document covers.
This is the next level down: editing an individual object's own geometry —
vertices, edges, faces — the same way Blender's Edit Mode works, entered from
a specific placed instance in the scene, not a separate standalone modeler.

## The foundational gap

Checked directly against the code, not assumed: `ce::Mesh`
(`Source/Render/Scene/Mesh.h`) is purely GPU-resident. `Upload()` pushes
vertex/index data into GL buffers and nothing is retained on the CPU side
afterward — no vertex positions to read back, no face/index list, no
adjacency (what faces touch a given vertex, what an edge loop is). None of
the operations below are possible until an actual CPU-side editable mesh
structure exists. That structure — at minimum a vertex array, a face/index
array, and some way to answer adjacency queries — is the real prerequisite
this whole feature sits on, not a detail to fill in later. Building it comes
before any of the UI/interaction work below.

## Asset vs. instance ownership

Meshes are shared, reusable assets (`MeshRenderer::mesh` is a `shared_ptr`,
deliberately shared across every placed instance of the same asset — see its
own header comment). Editing geometry in place must never mutate that shared
asset, or every other placed instance sharing it silently deforms too — the
same failure class the Material-sharing bug (`MaterialsPanel::PushToRegistry`
writing straight into a shared `Material`) already has, found earlier this
session and not yet fixed. Vertex edit mode must not repeat it.

The resolution, agreed in this planning session:

- An asset (the file) is always the shared, canonical source.
- Placing an instance in a scene references that asset by default — cheap,
  no duplication, as long as nothing's been locally edited.
- The moment an instance is about to be geometry-edited, it must first be
  **detached**: its mesh reference is replaced with an owned, embedded copy
  of the actual geometry, saved directly in the scene file rather than as a
  pointer back to the asset. From that point the instance is fully
  independent — editing it can never affect the original asset or any other
  placed instance.
- Detached instances need a real, stable identity — an `ObjectID` — since
  the scene serializer currently persists the raw `entt::entity` runtime
  handle as its "id" (`EngineSceneSerializer.cpp`), and that handle is not
  stable across loads (ECS handles get reused as entities are created/
  destroyed). Exact ID scheme (counter, GUID, etc.) is intentionally
  unspecified — what matters is that it's a real, persistent, unique value,
  tracked in an instance registry, not the volatile ECS handle.
- The same copy-on-write principle should extend to material edits too
  (the existing shared-Material bug), as a natural follow-up once this
  pattern exists for mesh geometry — not built as part of this pass, but the
  mechanism should be written so it isn't mesh-specific.

## Object Properties panel

The existing "Transform" dock panel (built this session — Position/Scale/
Rotate modes, local/world space, grid + angle snap) is renamed **"Object
Properties"** and restructured into stacked, independently collapsible
sections so more can be added over time without the panel becoming
unmanageable:

1. **Transform** (existing content, unchanged) — stays first/top, since it's
   what's used most.
2. **Metadata** — `ObjectID` (read-only) and `Name` (editable) for the
   selected instance. `scene::Name` already exists as a real component
   (`Source/Scene/Components.h`) and already backs what the Hierarchy panel
   displays — reuse it directly, don't add a second name field.
3. **Edit Object** — a single button whose label is the state machine:

   | Instance state | Button reads | Action |
   |---|---|---|
   | Still shared (never locally edited) | **Detach** (tooltip: "Detached objects drop the reference and are fully editable.") | Duplicates the mesh into an owned, embedded copy; drops the asset reference |
   | Detached, not currently editing | **Edit Object** | Enters edit mode |
   | Currently editing | **Save** / **Cancel** (two buttons) | Save commits the session's edits and returns to "Edit Object"; Cancel discards them and reverts to the mesh as it was on entry, also returning to "Edit Object" |

   The button label is itself the shared/unique indicator — no separate UI
   needed to show which placed objects are still asset-backed vs. already
   detached.

## Entering edit mode

- Every other entity in the scene is hidden (not dimmed — actually hidden)
  for the duration of the edit session. Only the object being edited, the
  grid, and ambient/global lighting remain visible — an isolation view, not
  a fade.
- The object being edited renders as wireframe with its vertex/edge handles
  visible, instead of solid-shaded.
- Camera fly (right-click hold, WASD) keeps working exactly as it does in
  object mode — you can still navigate freely while editing.
- A selection-mode toggle appears (Vertex / Edge / Face), the same three-way
  concept as Blender's 1/2/3 keys, determining what clicking picks and what
  the gizmo below acts on.

## Core operations (researched against Blender's actual Edit Mode)

- **Move / Rotate** selected vertices/edges/faces — reuses the exact gizmo
  system built this session (Position and Rotate modes, same handles, same
  drag math) aimed at the selected geometry instead of a whole object's
  transform. Not much new interaction code — the same machinery, a
  different target.
- **Extrude** — the primary "add geometry" operation: duplicates the
  selection and builds the connecting geometry to the new position.
- **Merge** — collapses selected vertices into one (at their center, or at
  the first/last selected).
- **Delete** — not a single action: delete vertices vs. delete edges (keep
  vertices) vs. delete faces (keep edges) vs. dissolve variants, since
  removing a vertex has ripple effects on whatever edges/faces touch it and
  the right cleanup behavior differs by intent.
- **Loop Cut** — adds an edge loop across a face ring. Not explicitly named
  in the original ask but a near-universal basic modeling operation, worth
  including.
- **Normals** — two genuinely different things colloquially both called
  "normals":
  - True face-winding normals: **Recalculate** (auto-fix outward/inward
    based on geometry) and **Flip** (manual invert). This affects culling
    and lighting for real — the same class of bug as the inverted-winding
    sphere found and fixed earlier this session.
  - Shading interpolation: **Smooth** vs. **Flat** — doesn't change geometry
    at all, only how normals are interpolated across a face for lighting.
    Worth keeping conceptually distinct from true normals in the UI.

## Explicitly out of scope for this pass

- Local-space rotation for object-mode gizmos (Euler decomposition math,
  deferred separately, unrelated to this feature).
- A full general-purpose modeling toolkit (bevel, boolean, subdivision
  surfaces, sculpting, UV editing, etc.) — "basic capabilities for now,
  nothing fancy yet."
- Applying the copy-on-write/detach mechanism to materials — noted as the
  natural next use of the same pattern, not built here.
