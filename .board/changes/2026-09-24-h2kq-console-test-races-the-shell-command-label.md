---
id: 5P0Q
type: work
status: inbox
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
source: 'Measured during #7Z08 exact-tree verification, 2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [H2KQ, 7Z08], github: null}
---
# H2KQ console test races the shell command label on a clean build

## Issue
Exact-tree verification for #7Z08 failed at tests/h2kq_cases.h:66: h2kqBusyText(pane).contains(QStringLiteral("sleep")). The wait at line 63 stops as soon as "Esc stops" appears, before the shell poll resolves the program name. The shared checkout already contains another session's uncommitted fix waiting for "sleep… · Esc stops". Queuecontract passed; no recall case failed.
