---
id: R8PK
type: work
status: done
labels: [bug, switchboard]
assignee: codex
rank: m
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [d4ac69d7872aabf184794faa4cc8366f22f3bbe9], evidence: [docs/qa_evidence/2026-09-21-restore-switchboard-card/README.md], related: [XAME], github: null}
---
# Restore the open Switchboard card

## Issue
when you close and re-open. i'd like it if the state of the switchboard pane was saved. eg, if a card was open, it should re-open. right now, it just shows the switchboard main page and closes the card.

## Plan
**Goal:** Restore the open card, selection and filter with the saved pane.
**Findings:** ToolPane::node saves folds and sorting; RelayWindow::buildNode restores them but neither remembers navigation.
**Steps:** Add BoardView navigation state; save and restore it through the layout; test delayed loading, missing cards, and returning to the list.
**Risks:** Restore must wait for board data and must not reopen a deleted card or treat a selected row as an open card.
**Verify:** Targeted boardpane tests and an Xvfb run with isolated configuration.

## Execution Summary
The layout saves BoardView navigation (open card, selected card and filter), restores it after board data arrives, and schedules saves when the open card or filter changes. Missing cards fall back to the list. Existing folds, labels and sorting remain part of the layout.

## Tests
`ctest -R ^boardpane$`

### Check 2026-09-21 21:37
- passed · ctest:boardpane — ctest -R boardpane passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
history: thread
## QA checklist
- [ ] Open a card, quit Relay, relaunch and confirm the same card opens.
- [ ] Return to the list, quit and relaunch; confirm the list remains open.
- [ ] Remove a saved card before restart and confirm the board still opens normally.
