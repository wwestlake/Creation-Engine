# Common world runtime

[Authoritative gameplay sessions](AUTHORITATIVE_GAMEPLAY.md) use the same world runner.

Play in Editor, local game windows and the dedicated server use `RuntimeWorldRunner`. Each instance owns its runtime content source, current scene, transition queue, fixed step and context ID. It is bound to one world, Jolt simulation and FRust host. The owner thread runs the lifecycle and ticks. Desktop hosts acquire input and present frames. The server uses the same sequence with no device polling or render surface.

## Execution and lifecycle

- The default simulation tick is 1/30 second. The server retains its tick-rate and step-duration launch options. Long scheduler pauses never become a larger desktop simulation step.
- Each tick runs EngineCore scripts and foundation gameplay, then FRust pre-physics behavior, Jolt advancement, desktop player processing if present, and FRust collision hooks. The runner applies queued scene transitions at the end of the tick.
- A transition resolves an in-game scene ID first and then its authored name. The target scene data comes from the project VFS. The tick counter stays monotonic as the runner deactivates old objects, clears their physics state and begins the new scene. An unknown scene or unreadable document leaves the live scene active and reports a runtime error. The editor restores its pre-Play snapshot on Stop.
- Each desktop window owns its input state and physics instance. Only the active top-level window polls local game input. Local player possession remains a desktop service. The editor and client can use their existing authored input contexts. Headless ticks can run without a player or input hardware.

## Behavior code and callback ownership

Runtime starts resolve attached Behavior Pods from the copied project session. The saved graph or source is authoritative. Graphs use the same compiler as the Pod editor. The runtime compiles FRust with the existing JIT and holds the loaded plugins for the play session. Built-in FRust modules are resolved beside the executable. Additional sibling source modules can resolve under the game's VFS code directory. Stopping unloads gameplay plugins so a new session can see saved changes.

FRust's native callback ABI remains the same. Each synchronous plugin call installs a scoped execution frame on the owning thread. The frame identifies the world and, for an object callback, its entity. Nested calls restore the prior frame. Initialization, object lifecycle, simulation and unload callbacks all use this scope. The editor has a separate FRust host for authoring and preview tooling.

## Inspection and verification

The Developer API keeps its v1 command and transport contracts. The free-form runtime diagnostics object now includes `contextId`, `gameId`, `sceneId`, `stepSeconds` and `runtimeError`. The context ID identifies the host runtime across starts and stops. The existing `session` field identifies a single play session. The editor's normal API listener remains bound to Play in Editor when local game windows are opened.

The fixture in `tests/RuntimeParityFixture` uses authored scene trees with dynamic Jolt bodies and an attached FRust source Pod. The test compares position at the same ticks with an absolute tolerance of 0.0001 m and checks exact behavior counters, scene identity, repeated start and stop, and independent commands for the editor and a local client in one process. The native object behavior smoke test also interleaves two FRust hosts and checks that destroying the second leaves the first running. The desktop integration test requires a desktop session. Its opt-in fixture listeners use the existing Developer API fixture authorization.

The fixture compares CPU simulation on the same platform and build. Cross-platform determinism, GPU behavior and VR hardware verification require separate coverage. Ahead-of-time behavior artifacts and package export remain separate work.
