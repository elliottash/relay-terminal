---
id: JXWT
type: work
status: needs-qa-llm
labels: [feature, gui, keyboard]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzz
created: '2026-09-19'
source: pane 1, 2026-09-20
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-pane-move-past-page-edge/], related: [], github: null}
---
# Ctrl+E then Ctrl+arrow should place the pane (Ctrl still held)

## Issue
when i open a new pane with ctrl e and press down to move it below -> that should also work with ctrl down (check other pane controls with arrows to make sure that holding control doesnt bust that)

## QA checklist

Evidence: `docs/qa_evidence/2026-09-19-pane-move-past-page-edge/` (shared with #8G7E, whose landing `087659d7` carried this card's PaneLayout half onto main). `relay-panes-tests` 42/42 green there, including the placement-with-Ctrl cases.

- [ ] `Ctrl+E` then `Ctrl+Down`, Ctrl still held: the new pane is placed below (no dismiss).
- [ ] `Ctrl+Shift+E` then `Ctrl+Down`: places too (Shift riding along is accepted).
- [ ] With the konsole preset (or any keymap binding `Ctrl+Shift+Down`): the bound shortcut still wins and nothing is placed.
- [ ] `Ctrl+E` then `Alt+Left`: still focuses the pane to the left, as before.
- [ ] Releasing Ctrl and pressing the bare arrow still places, as before.
