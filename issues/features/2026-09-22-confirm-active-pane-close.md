---
id: PCBG
type: work
status: needs-verification
labels: [feature, panes]
assignee: agent
implemented_by: kimi/kimi-k3
session: c14c34a7-957e-488f-8bbe-d5d50b181712
rank: m
created: '2026-09-22'
source: Owner in Relay, 2026-09-22
links: {commits: [30677e96, 9fe3d3dc, 607cc093], evidence: [docs/qa_evidence/2026-09-22-PCBG, docs/qa_evidence/2026-09-22-tryit-PCBG], github: null, plans: [], related: []}
---
# Confirm closing active panes and let work continue in the background

## Issue
right now, if i X out of a pane or press ctrl w, it exits even if a job is active. lets change it where if you x out when its active , it opens a modal and says, exit now and stop job, exit now and continue in background, cancel. does that make sense?

put it on a card and deliver

## Done means
- Pane × and Ctrl+W offer stop and close, continue in background and close, or Cancel when work is active; Escape cancels and idle panes close normally.
- Continuing preserves the actual shell, agent, subprocesses and output. Background sessions can be reopened without restarting their work.
- Agent turns, delegated agents, worker jobs and shell children count as active. Closing the last pane keeps a usable Relay window when backgrounding; explicit app exit ends background sessions.

## Plan
Goal: prevent accidental job termination while closing panes.

Findings: `src/RelayWindow.h` deletes panes without a busy check; `src/Pane.h` destroys their shell and worker. `src/WindowManagerImpl.h` manages window lifetime and session reopening. Existing pane moves preserve processes and resolve callbacks against their new window.

1. Add a shared close-choice dialog and a pane activity query, covering foreground and background work.
2. Guard pane close paths, including the last pane. Preserve the live pane in a hidden managed window when backgrounding and retain a visible window for access.
3. Expose background sessions through Sessions and Actions; reopening reveals the same live pane. Warn before the final visible window closes with background work.
4. Add targeted lifecycle/dialog tests, build via `scripts/relay-build`, and exercise the GUI under isolated Xvfb. Land through `scripts/land.py` and record evidence for independent verification.

Risks: modal reentrancy if work finishes during the dialog; hidden-window ownership and callbacks; background shell children; concurrent edits in shared headers. Background work lasts only for this Relay process.

Verify: all choices via × and Ctrl+W, idle close, last pane, process identity/output after background/reopen, job finishing during confirmation, and final-window shutdown.

## Tasks
- [x] Preserve and reopen background sessions. <!-- t:bg -->
- [x] Implement activity detection and close choices. <!-- t:ee -->
- [x] Build, verify, and land with evidence. <!-- t:n0 -->

## Tests
`ctest --test-dir build -R activepaneclose` — dialog choices, Escape/Enter cancel, window-dismiss cancel (pass 2026-09-23)
`ctest --test-dir build -R "closedstack|closedlist|windowstate"` — close-path neighbours (pass 2026-09-23)
manual: docs/qa_evidence/2026-09-22-PCBG/live-results.json — 9 live Xvfb scenarios

## Execution Summary
Closing a pane (× or Ctrl+W) with active work now opens a modal — Close and stop job / Close and continue in background / Cancel (default, also Escape) — from `relay::paneclose::ask` in `src/ActivePaneClose.h`. Activity detection is `Pane::hasCloseWork()` in `src/Pane.h`: agent turns, guest turns, live subagents, worker jobs, a busy foreground process, or any non-zombie child of the pane's shell (catches `cmd &` and stopped jobs). `closePane` and `closeTab` in `src/RelayWindow.h` route through it, including the last-pane/last-tab paths; closeTab backgrounds or stops every busy pane in the tab.

Background moves the live `Pane` widget — shell, agent worker, subprocesses, scrollback — into a hidden managed window flagged `backgroundSession` (`src/WindowManagerImpl.h`: `newEmptyWindow(geometry, true)`, `backgroundPanes()`, `lastVisibleWindow()`). Reopening goes through the Sessions dialog's new Background tab (or the `sessions.background` palette action) and reveals the same live pane; window cycling clears the background flag. Backgrounding the very last pane first adds a fresh tab so a usable window stays; closing the final visible window while background sessions exist asks for confirmation first. Background work lives only as long as this Relay process, as the dialog's informative text says.

Evidence: `docs/qa_evidence/2026-09-22-PCBG/` (modal.png, background.png, live-drive.py, live-results.json — 9 live Xvfb scenarios passed, including process-identity preservation across background/reopen and stop actually killing the shell child).

## Try it
Open: `bash docs/qa_evidence/2026-09-22-tryit-PCBG/stage.sh`

A Relay window opens with one terminal pane whose shell is running `sleep 600` in the background. Close that pane with Ctrl+W (or its ×), read the dialog, and try *Close and continue in background* — then reopen the session from Sessions → Background and confirm it is the same live job. Escape and *Cancel* should leave the pane alone.

Question (about 2 minutes): did the dialog's three choices read clearly enough that you were confident your job would survive backgrounding — is this how you'd want Relay to guard a real build?

Expected: docs/qa_evidence/2026-09-22-tryit-PCBG/expected.md (sealed until you answer)
