---
id: BDXG
type: work
status: ready
labels: [bug]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzzzzw
created: '2026-09-19'
acceptance: a click on the "updated tasks" row unfolds it in place to the task list as it stood after that call — one row per task with its status glyph — and a second click folds it away; a backend with no fold layer still reaches the list; a test covers the click routing and the fold's rows
source: 'issues/bug_intake.txt, 2026-09-19: "clicking "updated todos" call didnt uncollapse."'
links: {plans: [], commits: [], evidence: [], related: [SHE3], github: null}
---
# Clicking the "updated todos" row does not unfold it

## Issue
clicking "updated todos" call didnt uncollapse.

## Cause

`backend/relay_core/tool_labels.py` gives `update_todos` the label `open: {"type": "todos"}`.
`relay::calllines::clickFor()` maps that to `Click::Todos`, and `Pane::callAnchor()` only anchors a row
as a fold when the click is `Click::Fold` — so the row is a `relay://open-call` link, not a fold, and
the ▸ it shows is a promise nothing keeps. In `Pane::openCallTarget()` the `Click::Todos` case is a
bare `break` (it was meant to open a task surface that was never wired), which falls through to a
`tool_output_get` for the details pane rather than unfolding anything.

## Decisions

- **The row is a fold, and the fold is the list.** Same shape as Claude Code's todo rows: under the
  row, one line per task with its status glyph (the glyphs `RequestLedgerModel::todoGlyph` already
  draws in the tasks panel), completed ones muted, in-progress ones in the accent ink. The list is
  the one that call left behind (the call's own stored detail), not the live list, so an old row
  still says what it said.
- A backend with no fold layer keeps the open-call anchor and gets the same list in the details pane.
- The last row of the fold links to the tasks panel (`/tasks`), the way other folds end with
  "open in pane".

## QA checklist

- [ ] Click "updated tasks" in a live turn: it unfolds to the list; click again: it folds.
- [ ] A row from an earlier turn unfolds to that call's list, not the current one.
- [ ] A restored pane (no call record) still unfolds, through the #EC58 fetch-by-anchor path.
- [ ] `ctest` and `./scripts/test.sh` pass.
