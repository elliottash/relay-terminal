---
id: 93WR
type: work
status: executing
labels: [feature, switchboard]
assignee: agent
rank: i1
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [R9G7, 7BM4], github: null}
---
# Fold the cards the agent closed itself into one row of the done list

## Issue
ok, coming back to the medium fold vs signals, i see the value of that.

write cards on both. medium folding seems like a no brainer. so implement it with opus.

## Decisions
- The fold is presentation, not a type (2026-09-20): a medium card is ordinary work that the agent closed itself, so it stays a work card in the done lane and the board folds it.
- Marker, no new field: when the agent closes its own card to `done` from a non-QA status, Relay stamps `verified_by` with the closing pane's provider/model, as it already does for a QA close. A self-closed card is one whose `verified_by` equals its `implemented_by`.
- Wherever done cards are listed, self-closed cards collapse into one row, "N closed by the agent", folded by default, unfolded by a toggle and by the same keys a section uses; a filter or a search that matches one shows it.

## Tasks

- [ ] Backend: stamp `verified_by` on an agent's own close from executing/in-progress; rows carry it; tests; format and protocol docs
- [ ] GUI: the folded row in the done lists, toggle, keys, saved with the layout, search shows matches; tests; evidence
