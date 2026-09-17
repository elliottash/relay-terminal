---
id: D60R
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzd6
created: '2026-09-17'
acceptance: right-click offers the listed actions, Navigate here moves the terminal, and one shortcut opens and closes the explorer
source: '`issues/feature_intake.txt`, 2026-09-17: explorer right click, navigate here, folder click closes it, shortcut to toggle'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# File explorer: right-click menu, navigate here, and a toggle shortcut

- Right-click menu in the explorer pane: **Navigate here** (sets the terminal's directory), open, open in a
  preview pane, copy path, copy relative path, reveal in the system file manager, new file/folder, rename,
  delete (with confirmation), and "Set as agent workspace".
- Decide whether Ctrl+click also navigates (proposal: no — single click opens, Ctrl+click adds to selection).
- Clicking the project folder in the header closes the explorer when it is already open.
- A shortcut that opens **and closes** the explorer (proposal: Ctrl+Shift+E, free in all presets).
