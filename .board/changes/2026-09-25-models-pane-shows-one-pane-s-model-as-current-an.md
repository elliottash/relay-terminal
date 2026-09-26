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
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [WBFM, 4SHY], github: null}
---
# Models pane shows one pane's model as "· current", and model routing skips the rank 1 draw on several paths

## Issue
Relay has no single current model, but Enabled, Pick order and Effort label the opening pane's model "· current" (e.g. kimi-k3), which reads as a global setting. Tracing it found that routing is inconsistent: new panes draw among Main rank 1, but restore, mode switching, labels, /swap and the no-list fallback take a saved or first-row model instead of choosing from Main/High/Flash with a random draw within rank 1.

> and trace why kimi is set as current model, i never selected that. in relay there is no single current model. so trace every piece where its picking "current model" and tell me, so we can resolve that. it should be picking main, high, or flash, and randomizing within rank 1.
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25

## Decisions
- Mode switches to High/Flash draw again from rank 1 every time; no per-pane mode pick is reused. ("2 draw again")
- Restored panes draw again from rank 1, unless the user picked that pane's model by hand in its session: then the manual pick is restored. ("1 draw again", refined: "can you track if a model was selected manually within a session? if so, it should restore that model.")
- `provider/preset` is no longer written on configure/switch nor read as a "last used" default. ("yes, i agree with #3. last used doesnt mean anything.")
— elliott · 2026-09-25

## Done means
- A pane records a manual pick: every user choice through `Pane::selectEntry` (picker, chips, `/model`, remote `model_pick`) sets a per-pane `picked` key; automatic changes (rank-1 draw, failover, mode switch) do not. `serializeNode` saves it; `initRestore` reads it.
- Restore: a pane with a saved manual pick starts on it (if still usable); a pane without one draws from Main rank 1 via `startEntry`, like a new pane. `routing-draws.jsonl` logs the draw.
- Mode switch High/Flash draws from that tier's rank 1 on every switch; `m_modePick` is not reused.
- `/swap`, mode labels and the profile status no longer name the first row as if it were chosen; labels say the tier draws among its rank 1.
- `provider/preset|base|model|extra` are not written by configure or model switches, and no pane or the Switchboard reads `provider/preset` as a default; with no usable Main list the fallback is rank-1 draw → first stored key → Relay Free → local. Custom endpoint settings and `provider/max_tokens` keep working.
- Failure looks like: a restored pane without a manual pick landing on its old model every time; a manual pick lost on restore; `[provider] preset=` changing in relay.conf after a pane switches model.
