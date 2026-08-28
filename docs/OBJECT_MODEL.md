# Creation Engine Object Model

Creation Engine uses composition rather than an Actor inheritance hierarchy.

- **Assets** are reusable resources such as meshes and materials.
- **Object definitions** are reusable recipes: asset references, default
  state, attached FRust behavior pods, and child definitions.
- **Object instances** are ordinary live Engine entities created from a
  definition. They retain their definition identity, editable state, behavior
  references, and hierarchy relationship.
- **Behaviors** are FRust pods. The object system stores their references;
  the FRust behavior scheduler will load and dispatch them in the next slice.

An object definition does not own a GPU resource or raw C++ pointer. It stores
an asset identifier. The viewport resolves that identifier through
`AssetCatalog` once it has a live OpenGL context.

Definitions can compose children. The factory detects direct and indirect
cycles, while allowing the same child definition to be reused in separate
branches. Definition identity, asset references, behavior references, and
per-instance state are serialized with the scene.
