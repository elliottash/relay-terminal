---
id: R3VZ
type: work
status: inbox
labels: [bug, land, subagents, workspace]
parent: 3MH4
discovered_from: 3KB7
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-26'
source: 'Observed delivering #BP15 and #3KB7, 2026-09-26; workspace wt1db244c507e8f826'
links: {plans: [], commits: [], evidence: [], related: [80X1, BP15, 3KB7, GKMW], github: null}
---
# Delegated agents reuse the parent's queue workspace when RELAY_SESSION_TOKEN is inherited

## Issue
A Relay subagent asked the queue for its own development workspace, but the inherited RELAY_SESSION_TOKEN overrode its requested session and reused the parent agent's active workspace. Both agents edited the same source tree and workspace card identity changed, defeating queue isolation.
