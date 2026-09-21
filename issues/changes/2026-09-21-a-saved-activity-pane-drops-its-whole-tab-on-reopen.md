---
id: ACT1
type: work
status: needs-verification
labels: [bug, panes, sessions]
assignee: claude-code
rank: h
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — found while landing #MDL1 t:a11'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-models-pane], related: [QT8C, MDL1], github: null}
---
# A saved Activity pane drops its whole tab on reopen

## Issue
Found by Claude Code, not reported by the owner: `PaneChrome::serialize` writes the Activity pane (card #QT8C) into the saved layout as an `internals` node and `buildNode()` restores it, but `relay::windowstate::isUsableNode` never listed that kind. An unknown node makes its whole split unusable and the tab is dropped with it, so a tab holding a terminal beside an Activity pane came back as nothing — scrollback, directory and conversation gone. The models pane hit the identical trap on 2026-09-21 (`bb5fba2b`, driven in `docs/qa_evidence/2026-09-21-models-pane/`, `l-after-restart.png` of the first run).

## Execution Summary
One line in `src/WindowState.cpp`: an `internals` node whose value is an object is usable, like `settings`, `testsuites` and `models`.

## Tests
`tests/windowstate_test.cpp`: an `internals` node is usable, a bare string is not, and a split of a terminal and an Activity pane is usable. `ctest -R windowstate` green.

## QA checklist
- [ ] Open an Activity pane beside a terminal, quit, reopen with no arguments: the tab is back with both.
