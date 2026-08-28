# Creation Engine Object Model

Creation Engine uses composition rather than an Actor inheritance hierarchy.

- **Assets** are reusable resources such as meshes and materials.
- **Object definitions** are reusable recipes: asset references, default
  state, attached FRust behavior pods, and child definitions.
- **Object instances** are ordinary live Engine entities created from a
  definition. They retain their definition identity, editable state, behavior
  references, and hierarchy relationship.
- **Behaviors** are FRust pods. The Engine host loads a pod once under its
  stable pod ID, then dispatches it once for every object instance that
  attaches that ID.

An object definition does not own a GPU resource or raw C++ pointer. It stores
an asset identifier. The viewport resolves that identifier through
`AssetCatalog` once it has a live OpenGL context.

Definitions can compose children. The factory detects direct and indirect
cycles, while allowing the same child definition to be reused in separate
branches. Definition identity, asset references, behavior references, and
per-instance state are serialized with the scene.

During a behavior call, `engine_current_object_entity()` identifies only the
object receiving that call. The behavior can use a separately granted Engine
capability such as `engine_set_position_x`; it is not given unrestricted
registry access. Pod source discovery and VFS-backed project packaging are a
separate asset-management concern from this execution contract.
