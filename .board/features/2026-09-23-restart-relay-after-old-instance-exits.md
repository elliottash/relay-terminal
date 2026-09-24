---
id: R7ST
type: work
status: needs-verification
labels: [feature, sessions, gui]
assignee: codex
rank: m
created: '2026-09-23'
source: 'Owner in a Relay pane, 2026-09-23'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-restart-R7ST/], related: [K6KP, N6R8, HDA9, 6WKR], github: null}
---
# Restart Relay only after the old instance exits

## Issue
should we also add a /restart command that will safely restart after the previous instance ends

The owner then asked: "implement it so that i can use it starting next time".

## Done means
`/restart` is a local Relay command in the prompt box and is discoverable in command help and Actions. It saves the current window layout and terminal scrollback through the normal clean-quit path, lets the old Relay process fully exit and release its layout lock, and only then launches the replacement. The replacement restores the same workspace without a `--fresh` or explicit `--workspace` argument. A failed launch leaves the saved state intact and gives the owner a clear way to start Relay manually. The command handles other open Relay windows and does not silently discard running agent work.

## Planning notes
The current `/update` implementation in `RelayWindow::updateApp()` launches the replacement before closing the old windows. Share a safe relaunch mechanism with `/update` rather than leaving the two paths with different ordering. The overlapping-process layout overwrite was measured and fixed separately in #K6KP; this feature should still avoid overlap entirely and verify the exact sequence in an isolated profile.

## Plan
**Goal:** make a restart restore the saved window set only after the old process has finished saving and released its layout lock.

**Findings:** `main.cpp` already saves scrollback and layout on `aboutToQuit`; `RuntimeDirs` has a portable PID plus process-start identity, and `/update` currently starts its replacement before closing the old windows.

**Steps:** (1) add a small wait-for-exit entry path to the Relay executable using the existing process identity; (2) route `/restart` and `/update` through one handoff that checks active work, launches the waiter, and quits through `aboutToQuit`; (3) expose `/restart` in help and Actions.

**Risks:** the running binary cannot gain a new slash command until the next launch. A cancelled restart must never leave a waiter that launches unexpectedly, and a launch failure must leave the current Relay running.

**Verify:** targeted process-wait tests, a Relay build, then an isolated GUI drive proving that the replacement appears only after the old PID exits and restores the saved tabs and session IDs.

## Execution Summary
`/restart` and the Actions entry start a detached copy of Relay with a hidden process-identity argument. That copy waits before creating `QApplication`, so it cannot acquire the saved-layout lock or open a window while the old process lives. The old process quits through `aboutToQuit`, which saves layout and scrollback. A completed `/update` now uses this same path. Running programs and agent turns prompt for confirmation; a cancelled or failed helper launch leaves the current Relay open. The replacement starts without `--fresh` or `--workspace` and therefore restores the saved window set.

The isolated GUI drive reopened two tabs after the old process exited. ![Replacement Relay with both restored tabs](docs/qa_evidence/2026-09-23-restart-R7ST/after.png)

## Tests
- `scripts/relay-build --target relay-runtimedirs-tests` — passed.
- `ctest --test-dir build -R '^runtimedirs$' --output-on-failure` — passed, including the exact-process wait and timeout case.
- `scripts/relay-build --target relay` — passed, build `2026-09-23.14H.07`.
- Isolated Xvfb restart drive — passed; old PID 1221051 quit before new PID 1222025 started, the saved layout had two tabs, and both scrollback files existed. [Evidence](docs/qa_evidence/2026-09-23-restart-R7ST/README.md).
