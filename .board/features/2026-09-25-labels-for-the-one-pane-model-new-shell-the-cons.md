---
id: E8V1
type: work
status: needs-verification
labels: [feature, agent-ui, panes]
assignee: codex
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U1)'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-one-pane-labels/], related: [P2W8, AGNT], github: null}
---
# Labels for the one pane model: "New shell", the console kind in the header, no more "helper agent"

## Issue
there are no longer terminal agents vs helper agents. there are just agents. a new pane is a shell pane, it starts with an agent thats attached to the shell. we could even replace the name "pane" with "shell". so ctrl+E is new shell.

## Done means
Keymap and palette call the terminal action “New shell”; placeholder console actions name their future cards. Docked agent labels omit “helper”, and terminal headers identify Shell and the SSH host. Targeted tests pass and an Xvfb capture shows the header and Options row.

## Plan
Slice 3 of #P2W8 (decision U1: keep "pane" for the rectangle; the Ctrl+E action is "New shell"; a console kind shows in the header; no label says "helper").

**Goal.** Every user-facing label reflects the one pane model. Identifiers, protocol values (`switchboard`, `helper` persist scope, `agent_role`) and file names do not change.

**Steps.**
1. Keymap descriptions (`src/Keymap.h`): `pane.splitRight` "New shell to the right (then ← ↑ ↓ places it)", the other split actions likewise; `tab.new` unchanged. Palette entries in `src/AppCommands.cpp` / `src/ActionPalette.cpp`: "New shell", and new entries "New Python console" and "New card" that are present but say "not built yet: #83YV / #Y2BA" in a status line until those cards land (so the vocabulary is visible now).
2. The docked agent row on Options, Actions, Sessions, Models and the Board list: "Helper Agent (Alt+Q)" → "Agent (Alt+Q)"; placeholders and tooltips that say "helper" say "agent" (grep `-i helper` in `src/SettingsPane.cpp`, `src/Conversations.cpp`, `src/ModelsPane.cpp`, `src/BoardPane.cpp`, `src/Pane.h`, `src/RelayWindow.h`). The Models role labelled "Helper agent" (for `switchboard`) becomes "System-pane agent".
3. Pane header: a `consoleKind` property on `Pane` (today always "Shell"; SSH shows "Shell · host") drawn as the first chip of a terminal pane's header, styled with the existing chip tokens; the Board/Options consoles show nothing (they are not leaves).
4. Docs: `docs/ARCHITECTURE.md` ("Agents are consoles" paragraph gets one sentence: the labels), `docs/KEYBINDINGS.md` or wherever the key table lives; help popup text in `src/Pane.h` ("switch terminal / agent").
5. Tests: `tests/keymap_test.cpp` label assertions; any test that asserts the old strings (`rg -n "Helper Agent" tests/`).

**Files.** Only the ones named above; `src/Pane.h` and `src/RelayWindow.h` carry other sessions' uncommitted hunks, so claim with `land.py begin`, land with `--dry-run`, and exclude any hunk you did not write.

**Verify.** `scripts/relay-build --check "New shell" && ctest --test-dir build -R keymap`; `rg -n -i "helper agent" src/` returns nothing user-facing; one screenshot of a pane header and the Options agent row under Xvfb to `docs/qa_evidence/<date>-one-pane-labels/`.

## Tasks

- [x] Keymap and palette labels: New shell, New Python console, New card <!-- t:xm -->
- [x] Docked agent rows and Models role label: no 'helper' <!-- t:d7 -->
- [x] Console kind chip in the pane header <!-- t:7z -->
- [x] Docs, tests, evidence <!-- t:bp blocked_by=xm,d7,7z -->

## Execution Summary
Landed as three commits on `main`: `521d3f97` (label sweep — Settings/Conversations/Models/Jobs panes, Models role "System-pane agent", Theme, `docs/KEYBINDING-PRESETS.md`), `2af0f59f` (Keymap action labels incl. "New shell (Ctrl+E)", `consoleKind()` chip in the pane header via `src/Pane.h`/`src/PaneUi.cpp`/`src/RelayWindow.cpp`, `docs/ARCHITECTURE.md`), `c3002af8` (Xvfb capture). Evidence: `docs/qa_evidence/2026-09-25-one-pane-labels/01-shell-and-options.png`. Targeted tests re-run 2026-09-25 by the umbrella session after landing: `ctest -R '^(keymap|input|programcompletion)'` — 3/3 passed. No user-facing "helper agent" string remains (`rg -i "helper agent" src/` clean of user-facing rows). Identifiers and protocol values (`switchboard`, `helper` persist scope) unchanged, per the plan.
