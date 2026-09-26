---
id: SR2T
type: work
status: discussing
labels: [feature, design, land, workspace, panes]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: Claude guest pane, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [3MH4, S84D, R3VZ], github: null}
---
# When is a pane's queue workspace created and released? Not for shell-only panes, and a closed pane should be resumable

## Issue
Today every pane with a project calls `Pane::prepareWorkspace()` at construction (`src/Pane.h:789`), so a pane used only for shell commands still leases a new worktree. Closing the pane calls `workspace_context.release` (`src/PaneRuntime.cpp:110`). Unlanded work is kept as `retained`, and `TreeManager.reacquire` gives it back only to the same session token. Open questions: should a worktree be leased lazily, on the first agent turn or file write, rather than when the pane opens? And how does a user return to a closed pane's workspace, from Sessions or by reopening the conversation? Card #S84D covers the quota-exhaustion symptom.

> when is a new worktree spawned and killed? is it whe a pane is open and closed? because for a new pane i might just want to do shell commands. and if ic lose a pane, i might want to come back to it.
> — elliott · [session:9e7d96426aef4f42b8f052f757109540](relay://session/9e7d96426aef4f42b8f052f757109540) · 2026-09-26
