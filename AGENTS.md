# Agent Instructions

## Scope

These instructions apply to the umbrella `Creation Suite` repository.

App-specific repositories under `apps/` may also have their own `AGENTS.md` files. When working inside an app repo, follow both:

- this top-level suite policy
- the app-local policy

Follow current user instructions and the umbrella's October 17 shared-workspace and task-completion policy. It supersedes the former per-agent workspace, branch, and build-drop arrangement.

## Do It Right Rule

When a task has a real, correct architecture and a smaller patch/workaround, build the real architecture. No shortcuts.

- If the correct fix has been identified in discussion with the user, do not default to a smaller/faster patch instead because it's later at night or the session is long. Say so and ask, don't just pick the smaller one.
- A defensive patch around a known-wrong pattern (e.g. special-casing one call site instead of fixing the pattern itself) is technical debt being created on purpose, not a neutral choice.
- This does not mean over-engineer routine work: an ordinary bug fix or small change to an already-correct, already-working system stays scoped to that fix. This rule is about architecture-level decisions specifically, where "the fast way" and "the right way" have already been identified as genuinely different things.

## Development Environment Rule

This workspace is a development/evaluation environment by default.

- Do not preserve legacy filesystem-first project behavior unless the user explicitly asks for backward compatibility.
- Do not keep dual storage authority alive "just in case" if the active task is a suite VFS/container cutover.
- If a suite-managed container/VFS path and a legacy folder/project-file path both exist, prefer making the suite path authoritative and removing the legacy runtime path rather than synchronizing both.
- Treat old folder-era project structures as disposable test data in this environment unless the user explicitly says real assets or historical projects must be preserved.
- Do not reintroduce filesystem-first fallbacks, migration glue, or dual-write logic as a default safety measure when the user's goal is to stop using the filesystem model.

## Path Rule

- No hard-coded filesystem paths in code. Ever.
- Do not hard-code absolute Windows paths, developer-machine-specific roots, fixed user directories, or baked-in local workspace locations into runtime code.
- Resolve locations through the suite settings/bootstrap system, shared storage helpers, configuration, or explicit user choice where appropriate.
- If code still depends on a specific filesystem layout, treat that as technical debt to remove rather than a pattern to extend.

## Shared Ownership Rule

`shared/` is the highest-risk collision area in this workspace.

- Only one writable owner should modify `shared/` at a time.
- If another agent is doing suite-platform work, do not also edit `shared/` in the same checkout.
- Do not treat another agent's untracked files as authoritative without explicit confirmation.

## Multi-Agent Rule

Owner decision, October 17, 2026: this app's submodule checkout is shared by all LLMs under `D:\CreationSuite-Workspaces\DjehutiSuite`. Follow the umbrella `AGENTS.md` and `docs/MULTI_AGENT_WORKFLOW.md` for shared-workspace, feature-branch, build-output, and assignment-closeout policy. The former per-LLM clones/worktrees and binary drop areas are retired.

Coordinate one writable owner per repository and one active build at a time. Hand off the existing checkout and working state. Do not create another clone/build area to switch LLMs, change branches underneath an active owner, or discard another agent's edits.

## Secrets Directory Rule

The `Secrets/` directory contains critical credentials and private keys.
- `Secrets/` MUST remain in `.gitignore` at all times.
- **NEVER** stage, commit, or push any file inside `Secrets/` to any remote repository.

### Branch Ownership Check

Before editing or committing, inspect the current branch and working tree. A shared checkout retains the previous agent's state; an agent prefix is descriptive, not permission to switch away while that agent is active. Continue another agent's feature branch only through an explicit handoff. Start a new task branch only after the repository is available and its existing work is accounted for.

## Integration Rule

Serialize integration within each repository. The agent completing the authorized assignment performs its commit/push/merge/default-branch push under the umbrella's Task Branch and Completion Rule; do not park finished work awaiting a separate integration assignment unless the user directs that.

For submodule work, finish and push the app's default-branch merge first. Then explicitly stage, commit, merge, and push the matching umbrella gitlink update. Coordinate umbrella ownership before changing that checkout. Keep unrelated edits and gitlinks out of the commit.

### Push Authorization Rule

The standing workflow authorizes pushing and merging the work in an assigned implementation task. Do not ask again merely because an old policy required per-action approval. This does not authorize unrelated repositories, deployments, credential changes, destructive cleanup, force-pushes, or new features. Stop and report a real authorization or repository-protection blocker when one remains.

## Commit Scope Rule

Before running `git commit` in ANY repo in this workspace — the umbrella repo, an app repo, or a wiki repo — run `git status --short` immediately before staging and stage only the specific files intended for that commit.

- Never assume the index is clean. Content staged earlier by another agent (or by you, earlier in a session) can silently ride along into an unrelated commit.
- Never use a broad `git add -A` / `git add .`.
- **Wiki commits contain wiki content only.** A commit made inside `wiki/` or any `apps/*/wiki/` checkout must never include changes from the paired code repo, and vice versa.
- If `git status` shows anything staged that you didn't just add yourself and can't explain, stop and ask before committing — don't push it along "because it was already there."
- This rule exists because it has already been violated once: a single new spec file committed via `git add <file>` (skipping a pre-commit `git status` check) swept in four unrelated submodule-pointer bumps that were already staged, and the resulting commit was pushed before anyone noticed. Submodule-pointer updates are the integrator's job per the Integration Rule above — they should never land as a side effect of an unrelated commit.

## Build Rule

Reuse the project's canonical build directory and outputs across all LLMs. Coordinate access and run one build at a time; do not create per-agent build folders, separate checkouts, or binary drops as the default answer to contention. Hand off or wait for the existing build owner.

### Build Configuration Rule

Use `Debug` builds by default for normal development, testing, and troubleshooting.

- `Debug` is the standard day-to-day development build.
- `Release` is only for intentional release work, such as a tagged version release or explicit release-build verification requested by the user.
- Do not assume `Release` just because a build is meant to be runnable.
- When reporting a build result, name the configuration you actually built.

### Incremental Editor Build Rule

The Creation Engine editor is a large JUCE/OpenGL executable. Normal source
iteration MUST use the incremental build entry point:

```powershell
.\scripts\Build-EditorIncremental.ps1
```

- Never pass `LinkIncremental=false` during normal iteration.
- Never force a clean editor link merely to verify that a source edit was
  picked up.
- A clean/full editor build requires explicit, in-the-moment user approval.
- Build standalone smoke-test targets only when their own source or linked
  library changed; do not run the full CTest suite as routine editor
  verification.

## Merge Rule

Do not merge just because code exists locally.

Before reporting something as complete or integrated, verify:

- it is committed in the correct repo
- it is on the correct branch
- any required submodule pointer updates are done
- the relevant build or smoke checks were actually run when possible

## GitHub Project Board Rule

Always use GitHub Issues and the official GitHub Project Board (**Creation Suite Road Map** - Project #19) to track feature work, tasks, and completion status.

- **Shorthand Definition**: Whenever the user says "create a task", "add tasks", "track tasks", or similar, it ALWAYS means: *"Create real GitHub Issues on the repository for the active app/component (`gh issue create`) and add them directly to the **Creation Suite Road Map** project board (`gh project item-add 19 --owner wwestlake --url <issue-url>`)"*.
- **Status Sync Rule**: Every time an agent completes a task, fixes an issue, or changes work status, the agent MUST immediately update the corresponding GitHub Issue and Project Board item on **Creation Suite Road Map** (Project #19), adding completion comments (`gh issue comment`) and explicitly setting the project board item Status field to **Ready for Testing** (`gh project item-edit --id <item-id> --project-id PVT_kwHOADBc_84Bet07 --field-id PVTSSF_lAHOADBc_84Bet07zhZF9co --single-select-option-id a822205a`).
- Do not use temporary local `.md` task files as the primary task tracker.

## C: Drive Rule

**Under no circumstances write, download, build, or install anything on the C: drive. Period.** No exceptions for size or a tool's default location (including vcpkg's default `C:\opt\vcpkg\downloads`/`buildtrees`) — redirect to a D:-drive path instead. See the root `AGENTS.md`'s C: Drive Rule for the full incident.

## LLVM / vcpkg Build Rule

**Never build, rebuild, or touch LLVM (`apps/CreationEngine/vcpkg_installed/x64-windows/`) — directly or as a side effect of any `vcpkg` command — without an explicit, in-the-moment yes from the user.** This includes running `vcpkg install` in manifest mode for any reason, since that reconciles the whole `vcpkg.json` dependency list (which includes `llvm`) and can silently trigger a full rebuild even when you only meant to add something unrelated. See the root `AGENTS.md`'s LLVM / vcpkg Build Rule for the full incident this is based on and what to do instead. If blocked, stop and ask — never act.

## Required Reference

For the full concurrency and handoff process, read:

- [Suite multi-agent workflow](../../docs/MULTI_AGENT_WORKFLOW.md)
