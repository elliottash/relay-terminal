---
id: BDXG
type: work
status: needs-qa-llm
labels: [bug]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 subagent (Claude Code session), 2026-09-19; follow-up 2c43e19 by Claude Fable 5.1 (the coordinating session)
rank: zzzzzzw
created: '2026-09-19'
acceptance: a click on the "updated tasks" row unfolds it in place to the task list as it stood after that call — one row per task with its status glyph — and a second click folds it away; a backend with no fold layer still reaches the list; a test covers the click routing and the fold's rows
source: 'issues/bug_intake.txt, 2026-09-19: "clicking "updated todos" call didnt uncollapse."'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-clicking-updated-todos-does-not-unfold/], related: [SHE3], github: null}
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

## Implementation (2026-09-19)

`Pane::callAnchor` now anchors `Click::Todos` as a fold, like `Click::Fold`, because the call's
detail *is* the surface the row would have opened. The backend's `detail()` for `update_todos` sends
the section under the new `tasks` style (§ 23.5) and builds it from the **result's** validated items
when the call ran — the ids and statuses Relay settled on, so a row from an earlier turn keeps its
own list — falling back to the arguments when the call failed.

`relay::calllines::appendTasks` draws one row per task: `taskGlyph(status)` then the text, completed
and cancelled muted, one in progress in the accent ink, blocked in the error ink — the tasks panel's
own inks. `taskGlyph` is now the single glyph table in the program: `RequestLedgerModel::statusGlyph`
calls it, so the fold and the panel it opens cannot drift (`relay-requests` links `relay-calllines`
for it). An emptied list says so rather than folding to nothing.

The fold's last row is `open the task list` over `relay://tasks/<pane>`
(`FoldOptions::openInPaneText`), routed by `Pane::openOutputTarget` to `openRequests()` with the
`tasks.fold` shortcut hint (WARP.md's standing rule: it is the mouse path to a panel that has a key).
A restored pane with no call record still reaches all of this, because `handleFoldReply` fills the
label from the worker's reply before asking for the fold's options (#EC58). A backend with no fold
layer keeps the open-call anchor and gets the same section as the call's detail in a preview pane.

Evidence, including a live Xvfb run driven against a stub provider (its `drive.sh` and
`stub-provider.py` are in the folder, so the scenes can be re-shot):
`docs/qa_evidence/2026-09-19-clicking-updated-todos-does-not-unfold/`.

## QA checklist

- [ ] Click "updated tasks" in a live turn: it unfolds to the list; click again: it folds.
- [ ] A row from an earlier turn unfolds to that call's list, not the current one.
- [ ] A restored pane (no call record) still unfolds, through the #EC58 fetch-by-anchor path.
- [ ] `ctest` and `./scripts/test.sh` pass.
