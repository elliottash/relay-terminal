---
id: 4SHY
type: work
status: executing
labels: [bug, models, keyboard, ui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: cae84571-aa12-45d5-b3a2-881fb1298087
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
source: Relay pane, current session
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Enter and Space do not toggle model checkboxes

## Issue
Enter and Space do not toggle checkable model options on the Models pane’s Enabled and Pick order tabs. Pick order also mutes some checked rows and marks a model as “current”; the current-model annotation is confusing in configuration tabs, and the muted rows need a clear reason. Make the keys toggle the current eligible row, remove that annotation from hosted configuration tabs, and clarify why rows are muted.

> “enter / space doesnt check / uncheck options in enabled”
> “nor in pick order”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
>
> “two weird bugs in \"pick order\": why are some checked items white and some gray? andy why does it say kimi-k3-current?”
> “it says kimi - current in enabled as well. not sure what that is setting because its not supposed to.”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
>
> “ditto in effort, its saying kimi -current”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25

## Done means
- On Enabled, Enter and Space toggle the current model’s availability checkbox.
- On Pick order, Enter and Space toggle the current ranked model’s “in box” checkbox.
- Hosted Enabled, Pick order, and Effort tabs do not label a model “current”; that marker remains available in the standalone picker.
- Muted Pick order rows expose whether the cause is unavailable/exhausted or outside the Alt+M cutoff; an “In” check means the row is inside the cutoff, not necessarily runnable.
- Each key causes exactly one persisted change and the focused regression tests pass.
