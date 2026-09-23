---
id: RCP9
type: work
status: discussing
labels: [feature, sessions, design]
assignee: codex
waiting_on: owner
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [PV7W], github: null}
---
# Generate a final recap when a session closes

## Issue
we might need to discuss, that those should always be generated for closed sessions so they get saved for here

## Discussion points
Relay currently generates a saved summary after the first assistant reply and refreshes it after five more turns or a compaction. Closing a pane stops its worker, so guaranteeing a final recap needs a separate background path or a deliberate wait on close. This would make a model call for each eligible session and may affect close latency or usage.

## Done means
- Decide whether a missing or stale recap should be generated automatically when a session closes.
- If yes, define how it runs without delaying or losing a closing pane, including failures and the case with no configured chores model.
