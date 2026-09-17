---
id: G152
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzv
created: '2026-09-17'
labels: [bug]
acceptance: taking control with Ctrl+H (and returning) leaves every pane's size unchanged in a three-pane layout
source: '`issues/bug_intake.txt`, 2026-09-17: "something weird happened with the panes. when i did ctrl + H, it made the pane (the 3rd one) extremely tiny."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+H shrinks a pane to almost nothing

## Report

Owner, 2026-09-17, three panes open: pressing Ctrl+H (take control) made the third pane extremely small.

## Suspects

- `setNative(true)` hides the whole composer (`m_composer->setVisible(false)`), which changes the pane's
  minimum height; the enclosing `QSplitter` then redistributes sizes and never restores them when the composer
  comes back.
- Splitter sizes are not saved around the visibility change; the new window-state saving (`src/WindowState.*`)
  may then persist the collapsed sizes.

## Fix direction

Keep the pane's size fixed across composer visibility changes: record the splitter sizes before hiding and
restore them after showing, or give the composer a zero minimum so hiding it does not change the pane's
minimum size.
