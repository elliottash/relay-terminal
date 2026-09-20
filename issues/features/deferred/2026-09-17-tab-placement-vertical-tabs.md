---
id: 072Z
type: work
status: deferred
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: 7j
created: '2026-09-17'
acceptance: a setting that switches placement at runtime, with screenshots of both layouts
source: '`issues/feature_intake.txt`, "allow top-side vertical tabs or a left side bar with horizontal tabs (vertical default)"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Choose tab placement: top tabs or a vertical tab sidebar

## Context

Potential feature request, deferred by the owner on 2026-09-17. For now Relay keeps
Konsole-style horizontal tabs along the top of each window.

The intake wording reads as swapped. The likely intent is two layouts: horizontal tabs on
top, or vertical tabs in a left sidebar, with vertical as the default. Confirm with the
owner before building.

## Notes for later

- `QTabWidget` with west-side tabs rotates text; Warp-style vertical tabs usually need a
  custom list with titles, directories and pane counts.
- Keep the left side for tabs; the planned agent and terminal palettes open on the right.
- Tab shortcuts (Ctrl+T, Ctrl+Tab, Ctrl+W) must work the same in both layouts.
