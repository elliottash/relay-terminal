---
id: JM2M
type: work
status: dropped
labels: [feature, gui]
rank: zzzzzz
created: '2026-09-19'
source: 'clean-commit session audit, 2026-09-19: uncommitted src/PaneUsage.* + tests/paneusage_test.cpp name "issue #CPUM", which does not exist in the tracker'
links: {plans: [], commits: [], evidence: [], related: [D03W], github: null}
---
# A pane's share of the machine: CPU and memory in the header chip and the tab

## Issue
A pane's own CPU / memory share, as /proc counters of the shell's and the worker's process trees turned into percentages, shown in the pane header chip and summed for the tab. Implemented in src/PaneUsage.{cpp,h} (CMake comment references issue #CPUM); no card had been filed when the work landed uncommitted in the tree.

## Resolution
Dropped 2026-09-19 as a duplicate of **#D03W** ("Per-pane CPU and memory meters in the header, tab
and session manager"), which is the same feature, is in `needs_qa_llm/` and carries the evidence
(`docs/qa_evidence/2026-09-19-pane-cpu-ram-meters/`). This card was filed by a clean-commit audit
that searched the tracker for the string `#CPUM` — the id the uncommitted code named, which never
existed — rather than for the feature, and so did not find #D03W. (`#CPUM` is no longer in
`CMakeLists.txt` either.) Nothing here is unique to this card; QA happens on #D03W.
