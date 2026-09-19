---
id: JM2M
type: work
status: in-progress
labels: [feature, gui]
rank: zzzzzz
created: '2026-09-19'
source: 'clean-commit session audit, 2026-09-19: uncommitted src/PaneUsage.* + tests/paneusage_test.cpp name "issue #CPUM", which does not exist in the tracker'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A pane's share of the machine: CPU and memory in the header chip and the tab

## Issue
A pane's own CPU / memory share, as /proc counters of the shell's and the worker's process trees turned into percentages, shown in the pane header chip and summed for the tab. Implemented in src/PaneUsage.{cpp,h} (CMake comment references issue #CPUM); no card had been filed when the work landed uncommitted in the tree.
