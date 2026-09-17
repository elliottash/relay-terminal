---
id: RDQ7
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzrd
created: '2026-09-17'
labels: [bug]
acceptance: a normal start opens exactly one window; restoring a saved two-window layout opens exactly two
source: '`issues/bug_intake.txt`, 2026-09-17: "when relay opens, its opening a weird small second window."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A small second window opens when Relay starts

Since window restore landed, starting Relay opens an extra small window beside the real one. Likely
candidates: the saved layout holding a stale window, the restore path creating a window before the first
one is adopted, or the new window chrome creating a helper window that is mapped by mistake.
