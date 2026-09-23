---
id: K6KP
type: work
status: needs-verification
labels: [bug, sessions, gui]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User report in Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-restart-layout-lock/], related: [8EXS], github: null}
---
# A replacement Relay can erase the previous panes and tabs

## Issue
i just restarted and all the panes and tabs from last time were gone

## Done means
- Starting a replacement Relay while the old process is still shutting down preserves the old process's saved tabs, pane layout, and scrollback.
- A process that failed to obtain the layout lock at startup never overwrites that layout later in its lifetime.
- A later clean launch reopens the preserved layout.

## Plan
**Goal:** prevent a fresh secondary window from replacing the saved window set during a fast restart.

**Findings:** `WindowManager::ownsLayout()` retries the lock on every save. The 10:43:56 replacement process started before the 10:43:57 old-process quit; its fresh window was then eligible to become owner and rewrite `state/windows.json`.

**Steps:** decide layout ownership once during startup, keep nonowners read-only, and reproduce the overlapping process sequence with isolated XDG state.

**Risks:** a secondary instance will remain read-only after the first owner exits. This is deliberate: a later launch can restore the preserved layout.

**Verify:** build Relay; run an isolated two-instance restart test that checks the layout and scrollback survive, then relaunch to confirm the original tabs reopen.

## Execution Summary
Startup now makes a single layout-lock decision. A secondary process stays read-only after the first owner exits, so its fresh window cannot overwrite or prune the saved window set. Isolated three-process reproduction: [drive and result](docs/qa_evidence/2026-09-23-restart-layout-lock/README.md). The original user's overwritten layout could not be reconstructed from the remaining state file; individual conversation files remain available through Sessions.

## Tests
- `scripts/relay-build --target relay` — passed, build 2026-09-23.10H.04.
- `bash docs/qa_evidence/2026-09-23-restart-layout-lock/drive.sh` — passed; two tabs and both scrollback files remained after the overlapping instance, and a later clean launch kept both tabs.
