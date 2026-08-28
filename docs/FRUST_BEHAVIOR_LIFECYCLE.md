# FRust Behavior Lifecycle

Creation Engine owns application and simulation timing. FRust behavior pods
participate in that schedule; they do not create a game loop or present frames.

1. The application registers explicit host capabilities and loads compatible
   FRust pods. FRust runs a pod's `on_init` once at load time.
2. Level setup calls `prepareLevel(tick)`. Each loaded behavior attached to an
   object receives `on_spawn(tick)` once.
3. Entering Play calls `on_begin_play(tick)` once per spawned object/behavior.
4. Every fixed simulation step calls `on_tick(tick)` for behaviors that began
   play. Rendering consumes the completed Engine state afterward.
5. Stopping Play calls `on_end_play(tick)` once per active object/behavior.
   The application and loaded pods remain available for the next Play session.
6. Before an Engine-owned deletion, `notifyObjectDestroyed(entity, tick)` calls
   `on_end_play` when needed, then `on_destroy(tick)`.
7. Reloading or closing a pod runs FRust's `on_unload` at the runtime boundary.

Each named object hook accepts the current Engine tick as an `i64`. During a
hook, `engine_current_object_entity()` identifies the one object receiving the
call. Host capabilities remain the only way a behavior can affect Engine data.
