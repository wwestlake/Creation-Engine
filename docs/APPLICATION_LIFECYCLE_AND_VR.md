# Creation Engine — Application Lifecycle & VR Subsystem

What the app does between "the user double-clicks the icon" and "the user is looking at their scene," and how VR fits into that without being forced on anyone. No node-graph, rendering, or gameplay content here — this is process and glue only.

## 0. Why this document exists

Every pass at Creation Engine so far has produced real, working features with no connective process holding them together: scene rendering works, asset import works, VR rendering works, but nothing defines what happens in the first ten seconds after launch, what "last scene" means, or whether VR is something the user asked for. The gap gets filled ad hoc, usually with a hardcoded stand-in (`SeedDemoScene()` generating a room in C++ instead of loading real data; OpenXR initializing unconditionally on every launch with no user control). This document is the lifecycle and VR spec those stand-ins should never have needed to exist. Every capability below maps to a specific, currently-missing or currently-hacked piece of behavior — nothing here is aspirational polish.

## 1. Design Philosophy — Non-Negotiables

* **No feature exists to paper over a missing process step.** If content needs to appear on screen, it comes from real, authored, VFS-backed data flowing through the same load path as everything else — never a C++ function synthesizing a substitute.
* **The user is never surprised.** Nothing the app does silently (session state, VR activation, project switching) should be invisible or unconfirmed when it has a visible effect.
* **Every optional subsystem defaults off.** VR is the concrete case now; the same rule applies to any future optional hardware/runtime dependency (control surfaces, network services). Opt-in, not auto-discovered-and-launched.
* **Failure degrades, it doesn't loop.** No missing runtime, missing headset, or missing pack should retry silently — it fails once, reports why, and leaves the app in a clean, usable state.
* **First run and the ten-thousandth run follow the same code path.** A first-time user creating their first project should exercise the same "create default scene from authored content" logic as a returning user creating scene #40 — never a special-cased bootstrap.

## 2. Application Lifecycle

### 2.1 First launch — no VFS configured

This is a **suite-level** concern, not Engine-specific — it happens once, on the very first launch of *any* app in the suite, not per-app (per the existing Storage Boundary Rule: the VFS root pointer lives in `suite-settings.json`, shared by every app).

**Confirmed by code audit (2026-09-01): this flow does not exist today.** `suiteVfsRoot` empty silently falls back to `Documents\Creation Suite\` (`SuiteStoragePaths.cpp`) with no prompt and no user awareness. This is the wrong behavior and is being replaced, not extended.

**Required behavior:**

* The very first time the user launches *any* app in the suite, before anything else happens, the app asks the user to supply a VFS location. This is a one-time, suite-wide gate — not per-app, not skippable, not silently defaulted.
* After that initial setup, **there is no filesystem fallback of any kind.** If the configured VFS location cannot be found — unset, folder missing, drive unplugged, whatever the cause — that is an **error state**, not a silent default. Alert the user and offer exactly two options: locate the VFS manually (repoint to a folder) or quit. The app does not proceed in a degraded/fallback filesystem mode under any circumstance.
* Once a VFS root exists but Engine specifically has never been opened in it before: Engine has no project to resume and should present the suite's project picker/creation flow, not silently invent one.

### 2.2 First project, first scene

* When a brand-new Engine game is created, `EngineGameDocumentStore::ensureInitialGame()` already does the right thing: it creates one game, one scene, and populates that scene via `CopyDefaultScene()` — a straight copy of the authored `DefaultScene.xml` bundled in the Engine Asset Pack. **This is correct and should not change.** It is the equivalent of Unreal's pre-populated default level or Unity's default scene template: real, inspectable, authored content, not runtime generation.
* The result should look and feel like opening Unreal or Unity for the first time — a populated, lit, navigable starter room — arrived at through the same VFS scene-load path every other scene uses.

### 2.3 Returning launch — resume where you left off

Two independent "last opened" memories exist and must both work:

1. **Suite level: last opened project.** Engine already reads `lastProjectId` from suite settings during `ensureProjectSessionActive()`. Confirm this is genuinely suite-wide (shared with other apps) rather than an Engine-local copy of the concept — if every app tracks its own "last project" independently, opening Station then Engine will not agree on what "last" means, which will read as a bug even though each app is individually correct.
2. **Engine level: last opened game/scene within that project.** Already tracked in `engine-settings.json` via `lastOpenedGameId`/`lastOpenedSceneId`. This part already works as designed.
* **Fallback order when the remembered scene/game no longer resolves** (deleted, moved, corrupted): try the game's declared entry scene → first scene in that game → run `ensureInitialGame()`'s default-scene creation as if this were a first launch. Never fall through to a blank viewport or a procedurally-generated placeholder — those are exactly the failure mode this document exists to close off.

### 2.4 Scene content rule (formal statement of the fix already made)

No C++ code may generate placeholder scene content as a substitute for a missing or unresolved scene. If a scene fails to load, that is a visible, reported error state with a real recovery path (2.3's fallback order) — not an opportunity to draw a procedural room instead. `SeedDemoScene()` violated this and has been removed; this rule exists so nothing like it gets reintroduced under a different name.

## 3. VR Subsystem — Opt-In, Configurable, Observable

### 3.1 Current state (the problem)

`ViewportComponent::newOpenGLContextCreated()` unconditionally constructs an `OpenXRProvider` and attempts a full session on every single OpenGL context creation, headset or no headset, every launch. There is no user-facing control, no persisted preference, and no visible state beyond console/log output. When no runtime or headset is present, this is wasted work on every launch at best, and a source of confusing flicker/failure at worst.

### 3.2 Target behavior

* **VR defaults to off.** A user who has never touched VR settings gets a normal desktop editor with zero OpenXR calls made, mirroring Unity's XR Plug-in Management being empty/disabled until a provider is explicitly added.
* **A real Settings → VR panel**, matching this codebase's existing settings-panel pattern, exposing:
  * **Enabled / Disabled** — the master switch. Disabled means no OpenXR calls happen at all, not even a cheap probe.
  * **Initialize on startup / Initialize on demand** — independent of "Enabled." A user can enable VR but still choose to launch to desktop and start VR manually from a menu action (mirrors Unity's per-platform "Initialize XR on Startup" toggle, and Unreal's explicit "VR Preview" action as distinct from just having the plugin enabled).
  * **Live status**, always visible while the panel is open: `Off` / `Not initialized` / `Searching for runtime…` / `Connected: <headset name>` / `Error: <reason>`. This is the single place a user checks to answer "is my headset seen or not" without reading a log file.
* **Cheap detection before expensive setup.** Before committing to `xrCreateInstance` → session → swapchains, do the lightest available check for "is a runtime and headset actually present" and fail fast and quietly if not. The full session sequence only runs once that check passes, and only ever once per explicit user action (startup-if-configured, or the manual "Connect VR" button) — never retried automatically in a loop.
* **This setting is per-machine, not per-project.** Whether *this computer* has a headset attached has nothing to do with which project is open, so it belongs alongside other local/machine settings, not saved into project or scene data.

### 3.3 What this deliberately does not cover yet

Hand tracking, controller ray/grab interaction fidelity, comfort options (vignetting, snap-turn), and locomotion modes are real, separate work already partially prototyped elsewhere. This document only covers *whether VR turns on at all and who decided that* — the interaction layer is its own epic once this foundation exists.

## 4. Open Questions

These need an answer before or during implementation, not assumptions baked in silently:

1. ~~Does a suite-level "first-run: designate VFS location" flow already exist?~~ **Answered 2026-09-01: no, it does not exist.** §2.1 now states the required behavior directly: one-time suite-wide prompt on first launch of any app, hard error-and-recover (locate manually or quit) on any subsequent failure to find it, zero filesystem fallback ever.
2. Is `lastProjectId` genuinely shared suite-wide across all apps today, or does each app currently keep its own copy of that concept? (Confirm before treating 2.3's fallback logic as already correct.)
3. Should the VR Enabled/Disabled + Initialize-on-Startup settings live in the existing suite-wide settings store, or is a VR-specific store appropriate given it's genuinely Engine's own hardware concern for now?
4. What counts as "cheap detection" on this project's supported OpenXR runtimes in practice — is there a real lightweight probe available, or does even a minimal check require touching the runtime enough that "cheap" is aspirational? Needs a spike, not a guess.

## 5. Research References

Patterns this design draws on directly:

* Unreal Engine: explicit **Editor Startup Map** / **Game Default Map** settings (Project Settings → Maps & Modes) — the loaded-on-open level is a project setting, not inferred at runtime. New projects ship a pre-populated default level. ([Epic docs](https://dev.epicgames.com/documentation/unreal-engine/changing-the-default-level-of-an-unreal-engine-project))
* Unity Editor: **Load Previous Project on Startup** preference, plus automatic remembering of the last-open scene within a project. ([Unity discussion](https://discussions.unity.com/t/how-to-choose-what-scene-unity-boots-up-in/533870))
* Unity **XR Plug-in Management**: providers are opt-in and empty by default; a separate **Initialize XR on Startup** toggle exists per build target, decoupling "is XR available" from "does it auto-launch." ([Unity manual](https://docs.unity3d.com/6000.1/Documentation/Manual/xr-plugin-management.html))
* Godot: the **Project Manager** is a distinct entry point from the editor itself, responsible for project selection before any editor state loads. ([Godot docs](https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html))

## 6. Delivery Breakdown

Tracked as GitHub issues under an epic on this repo, added to the Creation Suite Road Map board:

* **Epic: Application Lifecycle & VR Subsystem**
  1. Build the suite-level first-run VFS prompt + hard error-and-recover flow (remove the silent filesystem fallback in `SuiteStoragePaths.cpp`)
  2. Confirm and, if needed, unify "last opened project" as genuinely suite-wide (resolves Open Question 2)
  3. Implement scene/game load fallback chain (2.3) with visible error reporting on every failure branch
  4. Formalize the scene-content rule (2.4) — add a lint/review checklist item, not just a doc statement
  5. Design and implement the Settings → VR panel (3.2): enabled/disabled, initialize-on-startup, live status
  6. Implement cheap pre-flight VR runtime/headset detection ahead of full session creation (spike first, per Open Question 4)
  7. Gate all existing OpenXR calls behind the Enabled setting; verify zero OpenXR calls occur when disabled
