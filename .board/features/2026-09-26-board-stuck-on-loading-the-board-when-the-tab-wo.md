---
id: 4F5W
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: f864396a-45c5-4107-8b46-334e03bb9c4c
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: pane f864396a, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [S84D, 7M6E], github: null}
---
# Board stuck on "Loading the Board…" when the tab worker's workspace lease is refused

## Issue
After the 10:16 restart, Board panes sat on "Loading the Board…" indefinitely. The tab's Board worker `configure` raised WorkspacePreparationError (workspace quota 50/50), which aborted it before `board.configure`. The following id-less `board_open` then failed with "This project has no board", and the pane dropped that error because it carried no id, so it showed neither the reason nor a Retry.

> issue -- board isnt loading. it is stuck on "loading the board..."
> — elliott · [session:d36d6970d1dc4c9c8290d1f6dceeb822](relay://session/d36d6970d1dc4c9c8290d1f6dceeb822) · 2026-09-26

## Execution Summary
Root cause (worker.log, 2026-09-26T14:16:11): two tab Board workers' `configure` raised `WorkspacePreparationError: workspace quota reached: 50 of at most 50`. That aborted `configure` before `board.configure`, so the following id-less `board_open` answered `This project has no board`. BoardView ignores errors without its request id, so it stayed on "Loading the Board…" with no Retry.

Commit 89fbae2d (workspace wt1a8f33c606d2dfcf, submitted with relay-land):
- `backend/worker.py`: a `WorkspacePreparationError` now configures the Board before re-raising. Its text reaches the GUI instead of "Protocol error". Every protocol `error` carries `request` (the failed message's type). `workspace_context` is imported at module level.
- `src/BoardPane.cpp`: an `error` with `request: board_open` on a board that never loaded replaces the loading line with the reason and a Retry.
- `docs/AGENT-SESSIONS-PROTOCOL.md` §19.1: documents both.

The quota filling up is #S84D's.

## Tests
- `pytest tests/test_workspace_context.py`: 5 passed. New `test_board_opens_when_the_workspace_is_refused` drives worker.py on a paused queue project: configure → error with request=configure, then board_open → `board` event with the .board root. Fails without the worker change.
- `relay-boardfilter-tests aWorkerFailureReplacesTheLoadingLine`: PASS. It fails without the BoardPane change.
- Pre-existing failures, also failing without this change: boardfilter `theRestOfAChunkedBoardOpenPatchesTheRowsIn`; test_board_protocol `test_the_launch_directorys_board_is_never_adopted_by_a_workspaceless_configure` (expects issues/board.yaml in the checkout); `test_the_owners_first_card_asks_first_and_lands_on_a_yes`.
- Evidence dir not committed: docs/qa_evidence is outside the workspace's sparse checkout.

## Done means
- A Board whose tab worker is refused a workspace lease still shows its cards.
- Any other failed `board_open` on a never-loaded Board shows the reason and a Retry, never an endless loading line.
