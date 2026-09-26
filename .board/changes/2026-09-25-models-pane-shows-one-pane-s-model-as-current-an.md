---
id: D49C
type: work
status: executing
labels: [bug, models, routing, ui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: cae84571-aa12-45d5-b3a2-881fb1298087
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: Relay pane, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [WBFM, 4SHY], github: null}
---
# Models pane shows one pane's model as "· current", and model routing skips the rank 1 draw on several paths

## Issue
Relay has no single current model, but Enabled, Pick order and Effort label the opening pane's model "· current" (e.g. kimi-k3), which reads as a global setting. Tracing it found that routing is inconsistent: new panes draw among Main rank 1, but restore, mode switching, labels, /swap and the no-list fallback take a saved or first-row model instead of choosing from Main/High/Flash with a random draw within rank 1.

> and trace why kimi is set as current model, i never selected that. in relay there is no single current model. so trace every piece where its picking "current model" and tell me, so we can resolve that. it should be picking main, high, or flash, and randomizing within rank 1.
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
