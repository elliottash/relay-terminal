---
id: YQH3
type: work
status: discussing
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# consolemode flakes on different cases per run under load

## Issue
The `consolemode` suite fails on different cases per run on this machine: one run fails the kill-timing case `tests/234z_cases.h:87` ("dead" wait on `kill(-group,0)`), the next fails transcript-text cases (`consolemode_test.cpp:2004/2007/720/721/733`). Same failures in a land verify slot building tip+one-change and in the shared `build/`, with and without that change — so it is not any one landing. `--234z-only` passes in isolation, pointing at load/timing sensitivity of the pane tests (several sessions build concurrently here).
