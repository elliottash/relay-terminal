---
id: YZZT
type: work
status: inbox
labels: [bug, guest]
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: issues/bug_intake.txt, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [B9V4], github: null}
---
# Mid-turn guest model switch leaves the old harness attached

## Issue
mid-turn set_model ONTO a guest preset is defective (found executing #B9V4, out of scope there): the harness is started at request time but the deferred landing never attaches it — on a native pane the harness:// config would reach _provider_for, on a guest pane the old harness keeps serving a pane whose config names the new guest
