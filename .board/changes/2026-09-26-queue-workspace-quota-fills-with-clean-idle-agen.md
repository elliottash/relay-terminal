---
id: S84D
type: work
status: discussing
labels: [bug, switchboard]
discovered_from: 3KB7
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-26'
source: '#MJG6 claimed-card verification, 2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [MJG6, R3VZ, BP15], github: null}
---
# Queue workspace quota fills with clean idle agent workspaces

## Issue
The repository reached its 50-workspace cap while a claimed-card verification tried to allocate a new workspace. Twenty active slots had no unlanded commits or pending jobs, including clean auto child workspaces for finished agents; the cap blocked useful work until two were released manually.
