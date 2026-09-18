---
id: GDWE
type: work
status: ready
rank: zzw
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: [], related: [AM2Z], github: null}
---
# @ file completion runs synchronous git on the GUI thread (multi-second block)

## Request
look for cleanup opportunities

## Findings
From the 2026-09-17 cleanup audit (verified by reading the code):

`Pane::refreshFileIndex` (src/main.cpp:6228) runs up to three synchronous `git` QProcesses with `waitForFinished(1000/2500/1500)` — worst case ~5 s blocked GUI. It is triggered from `updateAtPopup()` (src/main.cpp:6262) while typing `@` in the composer, on a 15 s cache. All waits have timeouts, so this is a responsiveness bug, not a hang.

**Suggested fix:** run the git calls asynchronously (or on a worker thread, once per cwd) and populate the popup when results arrive; the 15 s cache already defines the freshness contract.
