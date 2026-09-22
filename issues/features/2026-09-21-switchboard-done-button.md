---
id: D0NE
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [832e48498357f3e00f19e67996245873445e3f7c], evidence: [docs/qa_evidence/2026-09-21-switchboard-done/README.md], related: [], github: null}
---
# Switchboard Done button with undo

## Issue
the switchboard needs a button that says "Done (d)", with an undo toast.

## Plan
Goal: close a card with Done (d) and restore it with Undo.
Findings: src/BoardPane.cpp already routes status moves through the undo notice. The card document currently uses d for reply; Tab still reaches reply.
Steps: add the card button and d handling for card/list; reuse board_move and its acknowledged undo toast; test button, key, and undo routing.
Risks: preserve text editing and existing backend move gates.
Verify: targeted boardpane Qt tests under Xvfb with isolated configuration; build the exact landing tree.

## Execution Summary
Added Done (d) beside the card's status selectors. The d key closes the open or selected card through the normal board_move request, clearing manual section placement. The acknowledged write shows the existing toast with Undo (Ctrl+Z). Clicking the button triggers the shortcut hint; text fields retain normal typing.

Evidence: docs/qa_evidence/2026-09-21-switchboard-done/README.md

## Tests
`ctest -R ^boardpane$`

### Check 2026-09-21 21:37
- passed · ctest:boardpane — ctest -R boardpane passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
history: thread
## QA checklist
- [ ] Open a card, click Done (d), and confirm it moves to Done.
- [ ] Click Undo in the toast and confirm the previous status and placement return.
- [ ] Press d on a selected list card and an open card; type d in an editor without closing the card.
