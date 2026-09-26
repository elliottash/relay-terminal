---
id: FJ9S
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# bug: ctrl + shift + s would not open sessions from an aux pane (text editor)

## Issue
bug: ctrl + shift + s would not open sessions from an aux pane (text editor)

## Done means
Ctrl+Shift+S opens the Sessions & Projects manager from every focus context, including a tab whose leaves are all aux panes — the editable text editor pane, alone in its tab — where today it silently does nothing. The same holds for the aliases that share the path (`projects.open`, `globals.open`, the palette rows and the `app_open` "sessions" target). A second press still closes the manager when it is the active leaf (the #QWAS toggle rule), and behaviour from ordinary terminal tabs is unchanged. Failure would be recognised as: focus in a text-editor-only tab, pressing Ctrl+Shift+S, and nothing appearing.

## Plan
**Goal.** Make `sessions.open` (and the aliases that share its path) open the Sessions & Projects manager in a tab that holds no terminal `Pane` — the reported case is Ctrl+Shift+S from an editable text editor pane that is its tab's only leaf.

**Findings.**
- The key dispatch is fine: `RelayWindow::eventFilter` (`src/RelayWindowCore.cpp:173-246`) matches `sessions.open` from any focused widget and defers to `runActionNow` (`src/RelayWindowCore.cpp:250-324`), where `pane = target ? target : m_active.data()` and `sessions.open` → `toggleSessionsPane(pane, "sessions")` sits **before** the `else if (!pane) return;` guard at line 358, so a null `m_active` does not stop it.
- `toggleSessionsPane` (`src/RelayWindow.h:4054-4092`) closes only when the manager is the active leaf or holds focus (the #QWAS rule, `docs/qa_evidence/2026-09-20-sessions-key-toggles/`), else calls `openSessions(...)` (line 4062).
- The bug is in `openSessions` (`src/RelayWindow.h:4016-4044`): it resolves a workspace-owner Pane — `m_active`, else the open Sessions pane's `workspaceOwner`, else any pane in the current tab (`panes.first()`). When the current tab holds **no `Pane`** (a text editor moved to its own tab via `moveLeafToNewTab`, `src/RelayWindow.h:7967`, or a detached all-aux window) and no Sessions pane is already open there, it hits `ToolPane *existing = sessionsPaneIn(...); if (!existing) return;` (lines 4027-4028) and **silently does nothing**. That is the reported symptom. With `existing` present it instead focuses the manager and creates an owner pane beside it (`createPane(paneNode(existing->cwd()))` + `insertBeside`, lines 4039-4040) — the code's own idiom for an ownerless manager.
- `openSessionsFor` (`src/RelayWindow.h:4200-4218`) requires a non-null owner (`owner->cwd()` at line 4205), so the fix must produce one.

**Steps.**
1. In `openSessions` (`src/RelayWindow.h`), replace the silent early `return;` in the `!owner` branch: when the current tab has no `Pane` and no Sessions pane, create the manager — `createSessionsPane(cwd)` (defined `src/RelayWindowCore.cpp:462`) — and `insertBeside(...)` it next to the active leaf (`m_activeLeaf`, a `ToolPane` when all leaves are aux; cwd from that leaf's `cwd()`, else the tab's project cwd). Then fall through the existing lines: apply tab/query, `setActiveLeaf/focusLeaf`, and let the existing owner creation (`createPane(paneNode(...))` + `insertBeside`) run, so `openSessionsFor(owner, query, tab)` receives a non-null owner exactly as the `existing` path already does. Keep an early return only for a genuinely empty page (no leaf to dock beside).
2. Do not touch `toggleSessionsPane`'s close rule or the ordinary-tab resolution tiers; behaviour there is covered by the 2026-09-20 QA evidence and must not change.
3. Add a case to `tests/boardworkspace_test.cpp` (ctest `boardworkspace`) reading `openSessions` via the file-text pattern `windowSource()` uses (header plus `src/RelayWindow*.cpp` if that helper has been widened by #SZHQ follow-up): assert the no-owner branch no longer contains a bare `if (!existing) return;` and does insert a created Sessions pane beside the active leaf before calling `openSessionsFor`. `tests/test_action_catalog.py`'s dispatch assertions should stay green unchanged.

**Orchestration.** None — one function plus one test; no subagents.

**Risks.**
- Owner decision (question): the fix follows the existing idiom, so opening Sessions in an all-aux tab will also spawn a terminal `Pane` beside the manager to own it. If the owner prefers the manager alone in such tabs, `openSessionsFor` needs an owner-less variant — say so on this card and step 1 changes shape; the default here is the idiom, which is what a saved layout already does.
- An all-aux tab can also be a detached window; the same fix covers it, but the manual check should cover both.
- `boardworkspace` has known stale source-grep failures at tip (see `.board/changes/2026-09-23-boardworkspace-boardremote-boardexecute-suites-f.md`); do not chase those, only add the new case and compare against the suite's state before the change.

**Verify.**
1. `scripts/relay-build --target relay-boardworkspace-tests && ctest --test-dir build -R '^boardworkspace$' --output-on-failure` (new case green, no new failures versus tip).
2. `python3 -m pytest tests/test_action_catalog.py`.
3. Manual (`./build/relay`): move an editable file pane to its own tab, focus the editor, Ctrl+Shift+S → the manager opens beside it and takes focus; Ctrl+Shift+S again with the manager focused → it closes; from a normal terminal tab → toggle behaviour unchanged; repeat once in a detached window.
