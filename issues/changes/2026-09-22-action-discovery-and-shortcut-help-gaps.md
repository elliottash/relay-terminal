---
id: ACDG
type: work
status: needs-verification
labels: [bug, keyboard, actions]
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-22'
source: Owner in a Codex Relay pane, 2026-09-22; keyboard-system survey
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-keyboard-set/], related: [A9QR, A7SC, S3JH], github: null}
---
# Make command discovery complete and shortcut help consistent

## Issue
this is great. we shoudl definitely fix the identified gaps so card those first.

## Planning notes
Read-only source audit at c2a4b370, 2026-09-22:
- `src/RelayWindow.h::searchableActions()` returns the manually assembled `rootItems()` catalog. Public registered actions including `pane.restartShell`, `app.update`, `agent.highAgent`, and terminal zoom are absent from that root list. Several other omitted IDs have equivalent submenu rows; do not blindly duplicate those.
- `src/SettingsPane.cpp::actionSlashCommands()` advertises `/switchboard` for `board.open`, while `src/Pane.h::slashCommands()` makes `/board` canonical and marks `/switchboard` hidden.
- Shortcut and pane documentation has stale statements: `src/SettingsPane.h` says Actions and Options swap one pane, while `src/RelayWindow.h` hosts them independently; `docs/ARCHITECTURE.md` still lists Ctrl+P for split and Ctrl+Shift+G for screenshot, contrary to `src/Keymap.h`.
- Related cards implemented grouping, slash labels and section search; this request concerns remaining coverage and consistency, not repeating those implementations.

## Done means
Every public command is discoverable by name, with deliberate equivalents and contextual restrictions represented without duplicate rows. Displayed shortcut and slash hints match the actual command registry and the user's live bindings. Documentation describes the implemented behavior.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_action_catalog` (every Keymap action appears in the catalog or on the documented equivalents list; /board is the slash label)
`ctest -R settings`
manual: docs/qa_evidence/2026-09-22-keyboard-set/03-palette-search.png ("rest" finds restart, Update and zoom rows that were missing)

## Execution Summary
searchableActions() now appends every registered Keymap action that the hand-built tree lacks. Each gets its live shortcut, and a documented equivalents list keeps aliases from appearing twice. The slash label is /board, not /switchboard. docs/ARCHITECTURE.md key lists are corrected. Commits: cd7dcfcb, 3857b5fc (window-category rows under Relay).
