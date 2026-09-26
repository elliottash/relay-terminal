---
id: 2DWJ
type: work
status: inbox
labels: [feature, workflow, refactor]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: '#3BM5 discuss, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [3BM5, 243T], github: null}
---
# Continue the #243T split: move more Pane.h / RelayWindow.h bodies into .cpp files

## Issue
Most contested hunks happen in the hot headers (Pane.h is about 1 MB, RelayWindow.h about 480 KB), where sessions' edits interleave. Moving more inline method bodies into per-area .cpp files, as #243T started, directly reduces collisions and rebuild cost. Lesson 5 from #3BM5.
