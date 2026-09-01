# FRust Plugin Capabilities

Creation Engine hosts FRust as its gameplay and automation extension language.
Plugins use explicit `extern fn` declarations and the Engine only exposes
capabilities registered by `EngineFrustHost`.

## Current Capability Set

| FRust function | Signature | Purpose |
| --- | --- | --- |
| `engine_current_tick` | `() -> i64` | Reads the authoritative simulation tick. |
| `engine_first_transform_entity` | `() -> i64` | Returns the first entity with an Engine transform, or `-1`. |
| `engine_set_position_x` | `(entity: i64, position_x: i64) -> i64` | Sets an entity X position and returns `1` on success. |
| `engine_active_game_id` | `() -> String` | Returns the active Game ID. |
| `engine_active_scene_id` | `() -> String` | Returns the active Scene ID. |
| `engine_request_scene_transition` | `(scene_reference: String) -> i64` | Queues a transition to a Scene in the active Game. The reference can be the Scene ID or its authored name. |

Scene references are direct FRust strings. There are no hashes, numeric tokens,
or encoded identifiers in this API.

Entity values are opaque Engine entity identifiers. Plugins must treat them as
handles and pass them back to Engine capabilities rather than deriving their
own meaning from the numeric value.

The bundled `EngineLifecycle.frust` plugin demonstrates the contract by
finding a transform-bearing entity and moving it as simulation events arrive.

## Safety Rules

- Every capability validates entity handles before accessing the registry.
- Every registry operation takes `World::RegistryMutex()`.
- Capabilities are narrow functions, not raw pointers or C++ object access.
- Plugins declare every required host function in their manifest, so an
  incompatible plugin fails to load with a clear reason.

## Next Capability Groups

1. Entity lifecycle: create, destroy, name, and component-presence queries.
2. Transform and gameplay state: full position/rotation/scale, tint, and
   event-driven component changes.
3. Engine events: typed spawn, collision, input, and project events exposed
   through the FRust host lifecycle.
4. Visual authoring: NodeSystem graphs generate readable FRust source for
   gameplay automation; material graphs remain shader-generation graphs and
   generate GLSL through `ShaderComposer`.

The capability layer is intentionally incremental. A plugin gains only the
Engine functions it names in its manifest and the host has elected to supply.
