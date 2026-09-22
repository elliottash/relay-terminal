---
id: XJSN
type: work
status: inbox
labels: [feature, models]
assignee: unassigned
rank: n
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — left over from #MDL1 t:a15'
links: {plans: [], commits: [], evidence: [], related: [MDL1], github: null}
---
# A custom endpoint's extra request JSON has no home

## Issue
Not the owner's words: raised by Claude while retiring the advanced provider dialog for #MDL1
(`cdc4b9bb`). That dialog was the only place to type a per-request JSON body for a provider. A
custom provider stores a name, base URL, model ids and a reasoning style
(`backend/relay_core/customproviders.py`) and carries no request body, so the field had nowhere to
go. Nothing regressed for a built-in provider: `provider/extra` is still written by
`configurePreset` from the worker's own preset row.

## Discussion points
Whether anyone needs it decides the work. If custom endpoints must carry a body, the field belongs
on "+ add provider" in the models pane's providers tab, with a column in `customproviders.py` and
the worker sending it as the row's `extra`. If not, the card is dropped and the design says so.
