# Game Engine, Authoring Tool & Server — Capabilities Specification

What the system must be able to do. No memory layouts, no threading model, no compiler internals — those belong in a separate technical/implementation spec.

## 0. Purpose and Scope

This document specifies required capabilities only: things a designer, programmer, or player can observe the system doing. It intentionally excludes implementation detail — node IR formats, LLVM/JIT internals, thread architecture, ring buffer layouts, and database schemas belong in a follow-on technical specification once these capabilities are agreed. Three systems are covered: the authoring tool (the node-based engine editor), the runtime node system shared across domains, and the game server.

## 1. Design Philosophy — Non-Negotiables

These are constraints every capability below is measured against, not aspirational values.

* No feature ships because a commercial mega-engine has it. Every capability in this document maps to something this specific game or tool actually needs.
* The tool must run comfortably on consumer hardware roughly three-plus years old. A capability that requires a workstation GPU or excessive RAM just to idle does not belong here.
* The editor and the runtime are the same executable in different modes — not an editor bolted onto an engine, and not a separate desktop application talking to the game over a socket.
* Iteration speed is a first-class capability. Changing a node, a rule, or an asset must be visible in the running scene in under a second, without a full restart.
* All authorable logic — animation, materials, rules, events, audio — is expressed through the same node language. Someone who learns one domain's nodes already understands the shape of the others.
* Content is patchable, not hardcoded. Anything a second designer or a modder changes must layer over base content without editing base files.

## 2. Game Server — Capabilities

### 2.1 Session & Connection Management

* Authenticate incoming client connections before allowing them to join a session or world.
* Perform a handshake that confirms matching game/content version between client and server before gameplay begins.
* Maintain a live connection registry: who's connected, which character or session they own, and their connection quality.
* Detect client disconnects — both timeout and graceful quit — and tell the two apart.
* Support reconnection: a client that drops can resume its session and rejoin its character or world state without the server treating it as a new player.
* Synchronize a new or reconnecting client to current world state before letting it act — a real snapshot-plus-catch-up, not just "join and hope."
* Support multiple concurrent sessions, worlds, or instances from a single server process where the game calls for it (separate zones, matches, or shards).

### 2.2 Authoritative Rule Engine

The server is the final authority on outcomes. The client handles presentation and, at most, prediction — it is never trusted to report what happened.

* Validate every client input (movement, actions, item use) against server-known state before applying it, rejecting or correcting anything that violates game rules or physical possibility.
* Resolve combat (hit detection, damage, status effects, cooldowns) authoritatively on the server, independent of what the client reports.
* Resolve collisions and physics interactions (movement blocking, projectile paths, trigger volumes) using the server's own simulation, never the client's claim.
* Enforce game-mechanic constraints — inventory limits, resource costs, cooldown timers, eligibility checks — as gatekeeping logic a modified client cannot bypass.
* Run designer-authored rule logic — the same node graphs built in the authoring tool — inside this authoritative loop, so a rule change doesn't require a server binary rebuild, just a new compiled rule module.
* Flag statistically implausible client behavior (speed, action rate, impossible state transitions) as a hook point for anti-cheat, even if full anti-cheat tooling is a later project.

### 2.3 World, Player, and Character State

* Maintain a single authoritative global world clock/tick that every state change is timestamped against.
* Track entity state — position, orientation, velocity, active status effects — for every simulated object, not only players.
* Track player account state (identity, ownership, permissions) separately from character state (stats, inventory, position, progression), so one account can cleanly own multiple characters.
* Support partial state interest — a client receives state only for what's relevant to it (nearby entities, its own inventory), not the entire world, to keep bandwidth sane.

### 2.4 Persistence & Replication

* Persist world state and character state durably, so a server restart or crash does not lose player progress beyond a defined, acceptable window.
* Broadcast delta updates — what changed since the last tick or snapshot — to connected clients, rather than resending full state every tick.
* Reconcile client-side prediction with server-authoritative results smoothly, correcting rather than teleporting, wherever the game's feel requires it.
* Support save/load of world and character state independent of any specific client being connected, for offline persistence, backups, and migrations.

### 2.5 Deployment & Operations

* Package the server as a standalone, headless build deployable to a remote machine or container, with no dependency on the authoring tool or a display.
* Configure a running server instance (port, world/session parameters, player caps) via config file or launch parameters, without rebuilding the binary.
* Run multiple independent server instances from the same build — different worlds, shards, or matches — on one host or across hosts.
* Expose basic operational visibility: player count, tick rate/performance, uptime, and errors, in a form that can be logged or monitored externally.
* Hot-load a new compiled rules/content patch into a running server, or support a clean restart with a new build, without a full engine recompile — consistent with the patch-don't-rebuild philosophy used in the authoring tool.

## 3. Authoring Tool — Core Capabilities

### 3.1 3D Viewport & Navigation

* Render the actual game scene in 3D at interactive framerates on target (older/mid-range) hardware, using the same rendering feature set the shipped game uses — not a simplified proxy view.
* Let the designer fly, orbit, and pan through the scene in real time, at multiple speeds, with standard editor navigation: WASD plus mouse-look, focus-on-selection, and top-down/orthographic views.
* Display standard scene-editing aids: grid, move/rotate/scale gizmos, bounding volumes, and selection highlighting.
* Preview animation, particle/VFX, and material changes live in the viewport as they're authored, without a separate "play to see it" step for routine checks.

### 3.2 Scene Composition & Asset Placement

* Place, duplicate, move, rotate, and scale assets directly in the 3D scene with immediate visual feedback.
* Organize placed objects into a scene hierarchy/grouping structure for managing complex levels.
* Snap placement to grid, surface, or other objects where useful for level construction.
* Override properties per placed instance — this specific torch is lit, this specific door is locked — without forking the base asset.
* Search, filter, and browse the available asset catalog when placing objects, instead of hunting through folders.

### 3.3 Rule & Logic Design Surface

* Author game rules, state machines, and event responses visually, through the same node system described in Section 4, directly against objects and systems in the currently open scene.
* Bind node graphs to specific entities, entity types, or global systems — a graph can drive "all goblins" or one specific door.
* Test authored logic in-editor (play-in-viewport / simulate) without a full export-and-launch cycle.

### 3.4 Immediate-Mode Tooling UI

* Present all editing panels, inspectors, and node graph editors as an overlay directly on top of the 3D viewport, in the same window and process as the running scene — no separate heavyweight desktop application.
* Keep the tooling UI responsive and low-overhead so it doesn't meaningfully compete with 3D rendering for frame time.
* Provide inspector panels for selected objects, nodes, and assets that update live as underlying data changes.

### 3.5 Live Iteration Workflow

* Apply a change to a node graph, material, or rule and see its effect in the running scene in under a second, without restarting the editor or reloading the level.
* Preserve editor and play state — camera position, selection, simulation state — across most content changes, so iteration doesn't reset the designer's context.
* Support undo/redo across scene edits, node graph edits, and property changes.

## 4. Node-Based Programming System — Capabilities

### 4.1 Unified Node Foundation

One visual language and one set of authoring conventions across every domain — drawing on the same mental models already familiar from audio-node and web-node programming.

* Provide a single node graph editor used for every domain — animation, materials/textures, game events/rules, and audio — rather than a separate bespoke tool per domain.
* Enforce typed connections between nodes: a bone-transform output cannot be wired into a color input, an event-trigger pin cannot be wired into a numeric data pin, and violations are flagged at author time, not runtime.
* Support both dataflow-style graphs (continuous values flowing through a chain — good for materials, animation blending, audio) and control-flow/event-style graphs (explicit "then do this" execution wiring — good for game rules and triggers), within the same tool.
* Allow graphs to be authored purely visually, purely as code, or as a mix — a node can be backed by a short script, and a script can call into node-graph-defined behavior, so a programmer and a designer can work on the same system without blocking each other.
* Support reusable sub-graphs/functions so common logic (a "take damage" sequence, a "blend to idle" pattern) is built once and reused, not copy-pasted into every graph that needs it.
* Compile authored graphs down to fast native execution rather than interpreting them node-by-node at runtime, so visual authoring doesn't meaningfully cost more performance than hand-written code.
* Support live patching: editing a graph while the game or scene is running updates behavior immediately, with no rebuild-and-relaunch cycle.

### 4.2 Animation Node Domain

* Build animation blend trees and state machines visually — idle/walk/run blending, layered upper/lower body, additive poses.
* Drive skeletal animation: skinning, bone hierarchies, IK targets and constraints, and root motion.
* Sample and manipulate animation curves — timing, easing, procedural motion — as first-class node data.
* Trigger and respond to animation events (footstep frame, hit frame, animation-complete) that other systems, including game-event graphs, can react to.
* Preview animation graphs live against the actual skinned character in the viewport.

### 4.3 Texturing & Material Node Domain

* Build materials visually as a graph of texture samples, math operations, and PBR channel outputs — albedo, normal, roughness, metallic, emissive, and similar.
* Author procedural textures and effects (noise, gradients, patterns) directly in the node graph, without requiring external texture authoring for every variant.
* Manipulate UV coordinates and texture tiling/mapping through nodes.
* Parameterize materials so one graph drives many visual variants — color tint, wear amount, glow intensity — including parameters driven at runtime by gameplay state.
* Preview material changes live, on real meshes under real lighting, in the viewport.

### 4.4 Game Event / Logic Node Domain

* Author entity lifecycle logic — spawn, destroy, enable/disable, state transitions — visually.
* Author trigger/response logic — zone entry, interaction, timers, condition checks — each wired explicitly to what it causes.
* Author global/world-level state changes (quest flags, world events, day/night-style systems) as graphs, instead of scattered hardcoded flags.
* Branch and gate logic with conditions — if/else-equivalent, comparisons, boolean logic — as standard graph nodes.
* Call into or trigger animation and audio graphs from event graphs, and vice versa, so systems communicate without custom glue code per pair.
* Run identically whether the graph ends up executing client-side (cosmetic/local) or server-side (authoritative) — same node language, compiled for the context it runs in.

### 4.5 Audio Node Domain

* Patch together audio playback, mixing, and routing visually, using the same mental model as existing audio-node tools.
* Trigger sounds from game events and animation events authored in the other domains.
* Support parameterized/adaptive audio — volume, pitch, filter driven by gameplay state such as speed, health, or distance — via exposed parameters, the same pattern used for materials.
* Preview audio graphs live during scene playback in the editor.

## 5. Asset Management — Capabilities

### 5.1 Unique Asset Identity (FormID-Style)

* Assign every asset — mesh, texture, material, skeleton, audio clip, rule/logic graph — a unique, stable identifier that all references use, instead of raw file paths.
* Allow any asset to be replaced, patched, or overridden by ID without breaking anything that references it, and without editing the original file.
* Allow quick lookup and browsing of assets by ID, name, type, and tag from within the tool.

### 5.2 Layered Content & Modding

* Support a base content layer plus any number of override/patch layers — official DLC, community mods, local user tweaks — that combine at load time without modifying base files.
* Resolve conflicts between layers predictably, using a defined priority/load order, and surface conflicts to the author or user rather than silently picking one.
* Let a designer test a specific combination of layers ("base plus mod X, without mod Y") without reinstalling or rebuilding anything.

### 5.3 Asset Import & Optimization

* Import standard art-pipeline formats — common mesh, texture, and audio formats — into the engine's asset database.
* Convert imported assets into runtime-optimized forms (compressed textures, optimized mesh data) automatically at import time, not at runtime on every load.
* Stream large assets in and out of memory based on what's actually needed for the current scene or area, rather than loading everything up front.
* Report asset size, memory footprint, and dependency relationships — what references what — to catch bloat before it ships.

## 6. Build, Packaging & Deployment

### 6.1 Game Build / Export

* Export a playable, standalone build of the game — all required assets, compiled logic, no editor dependency — for the target platform(s).
* Strip editor-only data and tooling from shipped builds automatically.
* Support incremental builds, rebuilding only what changed, so iterating on a near-final build doesn't require a full repackage every time.
* Version builds and content packs so a specific shipped build can be reproduced or rolled back.

### 6.2 Dedicated Server Build & Deployment

* Export a headless, dedicated-server build from the same project and content as the client build, containing only what the server needs — no rendering, no client-only assets.
* Package the server build as a deployable artifact for a remote host or container, consistent with the operations capabilities in Section 2.5.
* Keep client and server builds in sync automatically from the same source content — no manual duplication of rules or data between the game and the server.

### 6.3 Patch & Content Distribution

* Ship incremental content and rule patches as additive layers (Section 5.2), for both the game client and the server, rather than full-game replacements.
* Version-check client and server on connect (Section 2.1) so mismatched patches are caught before they cause desync, not after.

## 7. Performance & Platform Targets

* The editor and the shipped game must run at real, playable framerates on hardware roughly three-plus years old — a hard constraint on every capability above, not an afterthought.
* No capability in this document should require background services, a separate heavyweight application, or resource reservations that a mid-range gaming PC can't spare while also running the game itself.
* Scene/level load times and asset streaming must not visibly stall interaction; loading happens incrementally or in the background wherever the game design allows it.

## 8. Debugging, Profiling & QA Tooling

* Provide live inspection of running game/entity state — values, active nodes, current animation or state-machine state — during play, in-editor and, at a basic level, in test builds.
* Provide performance visibility (frame time, per-system cost) so a designer or programmer can see what's expensive without reaching for an external profiler for routine checks.
* Log rule/event graph execution traceably — if something happened in-game, it must be possible to find which graph and node caused it.
* Support reproducible test scenarios (fixed seeds/starting states) for verifying that rule and physics logic behaves consistently.

## 9. Explicit Non-Goals — What This Deliberately Excludes

Every exclusion below is a documented decision, not an oversight. Cutting these is what makes the rest of this list achievable on a small team and a three-year-old gaming PC.

* No generic multi-purpose physics solver supporting every possible physical material and simulation type — only what this game's collision and physics rules actually need.
* No universal/legacy lighting model support for rendering techniques the game doesn't use.
* No separate heavyweight desktop editor application distinct from the runtime — the tool is the engine, running in a different mode.
* No general-purpose visual scripting VM with its own bytecode interpreter and garbage collector sitting between authored logic and execution.
* No support for asset or content formats, or legacy shader paths, the game doesn't ship with.
