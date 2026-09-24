---
id: W92V
type: work
status: needs-verification
labels: [feature, panes, sessions]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 42f336c3-adea-4056-8db6-6c8f23927aa5
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-w92v/], related: [BGRN], github: null}
---
# Open background agents in panes of the current window

## Issue
when you open agents from the "run in background" buttons, they should open in new panes, not new windows

## Done means
Opening a background agent from Sessions, a count, a notification, or a card places its live session beside a pane in the current Relay window. The hidden holder closes, its background count clears, and the agent conversation and running work remain intact. Opening does not create a visible Relay window.

## Plan
**Goal:** Opening background work uses a pane in the current window.

**Findings:** `RelayWindow::backgroundPane` moves a live pane into a hidden window; `WindowManager::focusPane` and other Open paths call `RelayWindow::revealPane`, which shows that hidden window.

**Steps:** 1. Add a window manager path that transfers a background pane into the active visible window beside its active pane. 2. Route background Open entry points through it. 3. Verify session and UI behavior.

**Risks:** Detaching the last pane closes its hidden holder asynchronously. Preserve the pane before that close and choose a visible target before detaching.

**Verify:** targeted build and an isolated UI test showing the window count remains stable while the agent opens as a pane.

## Execution Summary
Opening a background session now transfers its existing live pane from the hidden holder into the active visible window, beside the current pane, then closes the empty holder. Sessions list, header counts, notification and existing-session links route through this behavior. The pane's worker, conversation and session token are preserved. A visual interaction check remains for the separate verifier.

## Tests
- `scripts/relay-build --fast --target relay` — passed after the transfer and link changes.
- `git diff --check -- src/RelayWindow.h src/RelayWindowCore.cpp src/WindowManagerImpl.h` — passed.
- Manual verification: open a running background agent from Sessions, a header count, and a notification; confirm it becomes a pane in the same visible window and the live turn continues.
