# Djehuti Engine Container and Component Model

Status: canonical architecture baseline. This document defines the terms the
Engine uses. It replaces the earlier "everything is a component" document,
which incorrectly treated Project, Game, and Scene as component assets.

## 1. Containers

A **Project** is a Suite-wide VFS container. It owns assets and does not
belong to one application. Applications may organize their own documents
within the Project, but they do not own the Project or make its assets
private.

Djehuti Engine uses the following container hierarchy:

```
Project -> Game -> Scene
```

A **Game** is an Engine document container within a Project. It identifies
the scenes, game policy, input configuration, and other Engine-specific
documents that make up that game.

A **Scene** is an Engine document container and the authored composition
currently being edited or played. The 3D Scene Editor edits this container.
A Scene owns scene-object records. It does not become a reusable component
asset merely because it contains placed objects.

The physical VFS representation is an implementation detail, but the target
shape is a versioned Game directory and a versioned Scene directory containing
JSON documents. No durable document may require an operating-system path.

## 2. Component Assets

A **component asset** is a reusable, versioned asset described entirely by
its own data and references to other assets. It never embeds the bytes of a
referenced mesh, material, texture, code pod, or child component.

Every reusable building block is a component asset. Examples of component
capabilities include mesh presentation, material selection, code behavior,
animation, audio, and reusable child composition. These examples describe
capabilities, not mandatory Engine asset types.

A component asset may expose named slots, defaults, properties, and typed
connection points. It may reference other component assets to form a reusable
**component assembly**. An assembly is still a reference graph, not a copied
bundle of its referenced assets.

## 3. Mesh Components

A mesh component references model data and describes its mesh hierarchy. It
has named material slots, each with an optional default material-asset
reference. It may also have default child-component or code-component
references. Materials are independent assets; a mesh never owns material
bytes.

The Mesh Asset Editor opens this reusable definition. It presents the textured
mesh hierarchy and edits slots, defaults, child-component references, and
other reusable composition. It is not the Scene Editor and it does not edit a
particular placed scene object.

## 4. Scene Objects and Instances

A **scene object** is a record owned by a Scene. It identifies one placement
of a component asset or component assembly and records scene-specific data:

- stable scene-object identity;
- the component asset/assembly reference and version policy;
- transform and parent/child scene relation;
- scene-owned connections and instance configuration;
- explicit overrides of reusable defaults.

The same component assembly may be placed in many scenes or more than once in
one scene. Each placement has its own scene object record. Changing reusable
defaults does not silently overwrite an existing scene object's overrides.

If a component combination and its wiring are intended for reuse, they are
published as a component assembly asset. If they exist only in one Scene,
they remain scene-owned data.

## 5. Collision and Runtime Roles

A collider is configuration on a placed scene object or one of its placed
component instances. It is not intrinsic to a mesh component asset and it is
not a property of the Scene container. A placed object may have no collider,
one collider, or multiple colliders attached to selected mesh children or
groups.

Motion behavior is a separate runtime role. Static, kinematic, and dynamic
describe how a placed object participates in a particular Scene's simulation;
they do not describe a mesh asset. The editor must configure collision and
motion deliberately by purpose, not through a single "Add Physics" action.

## 6. Runtime Resolution

At runtime, Djehuti Engine resolves scene-object records and component
references into renderer, physics, animation, audio, and FRust runtime state.
The renderer's AssetCatalog is a GPU-resource cache and resolver only. It is
not the authoritative component graph, not the source of scene persistence,
and not an asset ownership boundary.

## 7. Current Migration Boundary

`ObjectDefinition` is the current compatibility representation for an early
component assembly. It currently supports mesh, Pod, and child-definition
references. New work must evolve it toward the general component-assembly
schema while continuing to read existing Object Definition assets and saved
scenes. It is not the final name or final data model.

`EngineSceneSerializer` currently writes one XML scene document containing
ECS-derived records. It is a compatibility persistence format. New scene
storage must migrate toward explicit JSON scene-object documents without
breaking existing project content.

## 8. Invariants

- Projects own assets Suite-wide; applications consume assets they understand.
- Containers contain documents and scene-object records; component assets are
  reusable reference graphs.
- A reference is never ownership of the referenced asset's bytes.
- Runtime pointers, OpenGL resources, Jolt handles, and EnTT handles are not
  durable asset or scene-document data.
- Reusable defaults and scene-specific overrides have separate storage and
  clear edit surfaces.
- All durable references use stable asset identifiers and version policies,
  never OS filesystem paths.
