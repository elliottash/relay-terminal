---
id: 78BN
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzx
created: '2026-09-17'
acceptance: one key makes a new pane to the right, and pressing an arrow immediately after moves the new pane to that side instead
source: '`issues/feature_intake.txt`, 2026-09-17: "for new pane, make it where, instead of splitting to the right or splitting down, it splits to the right by default, but if your next key stroke is up arrow, left arrow, or down arrow, thats where the new pane goes."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# One key for a new pane, with an arrow to choose the side

## Behaviour

- Ctrl+P (and Ctrl+Shift+P, card #5FY5) makes a new pane **to the right** at once, focused and running.
- While a short window is open (proposed 2 s, and until any other key or a click), Left, Up or Down re-docks
  the new pane to that side; Right keeps it. The pane's shell and agent are never restarted by the move.
- A transient hint under the new pane shows "← ↑ ↓ to place" and disappears with the window.
- Arrow keys typed after the window has closed behave normally (history, cursor movement).
- The separate "new pane below" key is dropped; the palette keeps explicit "New pane to the right / below /
  left / above" actions for people who want them bound.

## Notes
- Reuse the existing pane-move / re-dock code so a moved pane keeps its state, and `#ERES` (pane moves broken)
  should land first.
