---
id: 4XR8
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: beta
workstream: switchboard
rank: '3d'
implemented_by: unattributed (landed with the fabdba2 working-tree sweep)
created: '2026-09-19'
acceptance: the owner decides; if adopted, the key closes the pane through closeToolPane and the slow paths hint it once
source: 'found 2026-09-18 during #JN7X in the working tree of `src/RelayWindow.h`; relay-terminal-71, -8e, -9e and relay-free-hosted-inference each said it is not theirs, and neither intake file asks for it'
links: {plans: [], commits: [fabdba2], evidence: [], related: [JN7X], github: null}
---
# Ctrl+Shift+S pressed on a focused Switchboard closes it: an unclaimed uncommitted edit to adopt or drop

## Issue

`src/RelayWindow.h` holds an uncommitted edit nobody owns: at the top of `toggleBoardPane()` a
focused Switchboard is closed through `closeToolPane()` under a `m_boardClosedByToggle` flag (today
the key hands focus back to the terminal and leaves the board open), `closePane()` gains a one-time
`board.close` hint for the slow paths, and the flag is a new member. The same edit had made
`closeToolPane()` call itself for the Settings pane; relay-free-hosted-inference restored that one
line on the owner's instruction, 2026-09-19. Every #JN7X commit was built without these hunks.

## Decision needed (owner)

- **Adopt**: small and coherent, and closing is arguably what the key should do. Commit it as its
  own change with a line in the shortcut-hint registry docs.
- **Drop**: `git diff src/RelayWindow.h`, remove the three hunks.

Implementer's lean (Claude, #JN7X session): adopt.

## Resolution (found already landed by the 2026-09-19 board sweep) — adopted

The decision this card was waiting on is moot: the edit is on `main`. It went in with `fabdba2`
("Commit the shared working tree: the 09-18/19 batch and everything edited since"), which swept up
the uncommitted hunks this card describes, so it was **adopted** rather than dropped — the
implementer's own lean, but by a bulk commit rather than a decision, which is why the card never
moved.

On `main` today, `RelayWindow::toggleBoardPane()` closes a focused Switchboard through
`closeToolPane()` under `m_boardClosedByToggle`, and `closePane()` carries the one-time
`board.close` hint for the slow paths. The Settings-pane line that the same edit had broken was
restored separately on the owner's instruction, 2026-09-19.

The card also asked for a line in the shortcut-hint registry docs if it was adopted. That was
missing and is added in the same commit as this move (`docs/ARCHITECTURE.md`, "Shortcut hints").

## QA checklist
- [ ] Ctrl+Shift+S with the Switchboard focused closes it; with it open but unfocused, focus moves to it.
- [ ] Closing it by the slow path (button, palette) shows the `board.close` hint once, and never again.
- [ ] The Settings pane still closes the way it did — the regression this edit once caused has not come back.
- [ ] The hint respects the global "Shortcut hints" setting.
