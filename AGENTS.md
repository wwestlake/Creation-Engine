# Agent Instructions

## Scope

These instructions apply to the umbrella `Creation Suite` repository.

App-specific repositories under `apps/` may also have their own `AGENTS.md` files. When working inside an app repo, follow both:

- this top-level suite policy
- the app-local policy

If they conflict, stop and ask the user which rule should govern the task.

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

Do not use the same writable checkout as another agent.

If multiple agents need to work in parallel, each agent MUST use their designated workspace directory or git worktree:

- **Gemini Workspace**: `D:\000 Creation Suite` (Branch prefix: `gemini/...`)
- **Claude Workspace**: `D:\CreationSuite-Workspaces\CreationSuite-Claude` (Branch prefix: `claude/...`)
- **Codex Workspace**: `D:\CreationSuite-Workspaces\CreationSuite-Codex` (Branch prefix: `codex/...`)

Never assume that untracked files, half-finished edits, or local build outputs in a shared checkout are safe to build on.

## Secrets Directory Rule

The `Secrets/` directory contains critical credentials and private keys.
- `Secrets/` MUST remain in `.gitignore` at all times.
- **NEVER** stage, commit, or push any file inside `Secrets/` to any remote repository.

### Branch Ownership Check

Before committing anything, check `git rev-parse --abbrev-ref HEAD`. Branch names in this workspace follow an owner/topic convention (`codex/...`, `claude/...`, `gemini/...`, etc.) — if the current branch's prefix names a different agent, **that branch is not yours to commit on**, regardless of who happens to have it checked out right now.

- If the checked-out branch belongs to another agent, create and switch to your own branch (matching your own name prefix) before making any commit — do not commit to someone else's named branch just because it was already checked out.
- This applies even for small, low-risk changes (a doc fix, a wiki edit) — branch ownership isn't scaled by risk, same as the Push Authorization Rule below.
- This rule exists because it was already violated: work happened for an entire session directly on `codex/suite-save-20260729` — a Codex-owned branch by naming convention — without it ever being handed off.

## Integration Rule

Only one actor (the User) acts as the primary integrator.

### Pull Request Review Rule

From now on, agents MUST NOT merge feature branches directly into `main` or `master`.

- All feature work must be committed and pushed to a named agent branch (`gemini/...`, `claude/...`, `codex/...`).
- To land changes into `main`/`master`, the agent must create a GitHub Pull Request (`gh pr create` or output the PR creation link) and assign it for user review.
- The user will personally review, approve, and merge the Pull Request on GitHub.

The integrator is the only one who should:

- merge branches into `main`/`master`
- resolve conflicts
- update umbrella-repo submodule pointers
- declare work fully landed

If you are not explicitly the integrator, stop at a clean branch and report:

- repo
- branch
- files touched
- what was verified
- what remains risky or blocked

### Push Authorization Rule

`git push` is never inferred. It requires an explicit, in-the-moment instruction to push, from the user, in that exact conversation — not:

- a request that would merely be *satisfied* by something existing on the remote (e.g. "give me a link to that file" does not mean "push it")
- a prior approval to push something else, earlier or elsewhere
- the fact that a task feels finished or ready

If completing a request seems to require a push and you are not the designated integrator for that repo, **stop and say so explicitly** before doing it: name the repo/branch, say a push is needed to accomplish the request, and ask. Do not act first and explain afterward. This applies even to low-risk-looking pushes (a single new doc file, a wiki page) — risk level does not change the authorization requirement.

This rule exists because it was already violated: a request for "a link I can paste into a ticket" was read as implicit permission to commit and push, which it was not, and the resulting push also carried unrelated staged submodule-pointer changes along with it (see Commit Scope Rule below).

## Commit Scope Rule

Before running `git commit` in ANY repo in this workspace — the umbrella repo, an app repo, or a wiki repo — run `git status --short` immediately before staging and stage only the specific files intended for that commit.

- Never assume the index is clean. Content staged earlier by another agent (or by you, earlier in a session) can silently ride along into an unrelated commit.
- Never use a broad `git add -A` / `git add .`.
- **Wiki commits contain wiki content only.** A commit made inside `wiki/` or any `apps/*/wiki/` checkout must never include changes from the paired code repo, and vice versa.
- If `git status` shows anything staged that you didn't just add yourself and can't explain, stop and ask before committing — don't push it along "because it was already there."
- This rule exists because it has already been violated once: a single new spec file committed via `git add <file>` (skipping a pre-commit `git status` check) swept in four unrelated submodule-pointer bumps that were already staged, and the resulting commit was pushed before anyone noticed. Submodule-pointer updates are the integrator's job per the Integration Rule above — they should never land as a side effect of an unrelated commit.

## Build Rule

Avoid sharing scratch build folders across multiple active agents.

If an app repo has a fixed build-directory rule in its own `AGENTS.md`, respect it. If that makes safe parallel writable work impossible in that checkout, use a separate worktree or do not parallelize that repo.

### Build Configuration Rule

Use `Debug` builds by default for normal development, testing, and troubleshooting.

- `Debug` is the standard day-to-day development build.
- `Release` is only for intentional release work, such as a tagged version release or explicit release-build verification requested by the user.
- Do not assume `Release` just because a build is meant to be runnable.
- When reporting a build result, name the configuration you actually built.

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

## Required Reference

For the full concurrency and handoff process, read:

- [docs/MULTI_AGENT_WORKFLOW.md](docs/MULTI_AGENT_WORKFLOW.md)
