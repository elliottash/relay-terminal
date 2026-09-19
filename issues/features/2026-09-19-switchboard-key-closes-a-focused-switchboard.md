---
id: 4XR8
type: work
status: ready
labels: [feature]
component: [gui]
milestone: beta
workstream: switchboard
rank: '3d'
created: '2026-09-19'
acceptance: the owner decides; if adopted, the key closes the pane through closeToolPane and the slow paths hint it once
source: 'found 2026-09-18 during #JN7X in the working tree of `src/RelayWindow.h`; relay-terminal-71, -8e, -9e and relay-free-hosted-inference each said it is not theirs, and neither intake file asks for it'
links: {plans: [], commits: [], evidence: [], related: [JN7X], github: null}
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
