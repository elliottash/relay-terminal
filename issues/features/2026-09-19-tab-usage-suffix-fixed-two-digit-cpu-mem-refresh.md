---
id: MERX
type: work
status: in-progress
labels: [feature, gui]
implemented_by: glm/glm-5.3
rank: zzzzzzzz
created: '2026-09-19'
source: pane 1, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Tab usage suffix: fixed two-digit cpu/mem, refreshed every 5 s from the 5 s average

## Issue
for the tab headers, always show cpu 00% mem 00% and 01% or 05%, always use 2 digits, so they dont keep on widening and narrowing. update only once every ~5 secs or so, using the 5 sec average.
