#K3TY — in plan mode, the terminal agent or Switchboard agent plans orchestration

Prompt-level, Warp-style: both plan authors decide not just *what* changes but *how the work
is executed*, and both Execute prompts tell the executor to follow that. Landed as
**4a0cb8cd** (planning.py, board_plan_brief.md, BoardModel.cpp, the three python test files,
both docs) and **6011141c** (the boardmodel_test.cpp hunk, held back from 4a0cb8cd by a merge
conflict on another session's uncommitted `openCard` helper in the same file; landed once a
refreshed snapshot moved that edit into the merge base). Both landings passed land.py's
verify build of the exact tree.

## What changed

- `backend/relay_core/planning.py` — `PLAN_MODE_NOTE`'s `write_plan` instruction: when the
  work is big enough to split across subagents the plan carries an **Orchestration** block
  (each subagent's type and a one-line task, which steps run in parallel, which wait for
  which; only steps that touch no shared files may be parallel, writes stay with the main
  agent; a small plan gets no block). `execution_prompt()` appends `ORCHESTRATION_NOTE`:
  follow the block — start the listed subagents (independent ones in one response, so they
  run concurrently), wait before dependent waves, do yourself only what it assigns to the
  main agent, name any deviation in the final reply.
- `backend/relay_core/board_plan_brief.md` → **v2** (#K3TY): an **Orchestration** bullet in
  "What a plan holds", same rule, written for the terminal agent Execute hands the card to.
- `src/BoardModel.cpp` `executeTask`: both hasPlan variants add "Where the plan has an
  Orchestration block, follow it: parallel steps to subagents started together, dependent
  waves in order." The no-plan variants are unchanged and never mention Orchestration.
- Tests: `test_questions.py` (the note names Orchestration, after "numbered steps"),
  `test_sessions.py` `PlanModeTests::test_the_execute_prompt_follows_the_plans_orchestration_block`,
  `test_board_protocol.py` (a Plan turn's prompt carries the v2 brief), `boardmodel_test.cpp`
  (pins the new executeTask wording and that neither no-plan variant mentions Orchestration).
- Docs: `AGENT-SESSIONS-PROTOCOL.md` §6 (`write_plan` and Execute bullets) and §19.10 (what
  the task text names), `SWITCHBOARD-DESIGN.md` Plans bullet. No new messages or events.

## Test output (this machine, 2026-09-20)

`scripts/relay-build` — green, 48 s (12 artifacts stamped back).

    PYTHONPATH=backend python3 -m unittest discover -s tests -p 'test_questions.py'
    Ran 35 tests in 0.171s — OK
    PYTHONPATH=backend python3 -m unittest discover -s tests -p 'test_sessions.py'
    Ran 37 tests in 1.151s — OK
    PYTHONPATH=backend python3 -m unittest discover -s tests -p 'test_board_protocol.py'
    Ran 148 tests in 5.419s — FAILED (failures=2)

The 2 failures (`WriteTests.test_a_card_detail_read_round_trips_through_the_protocol`,
`ProbeAndImportTests.test_apply_creates_the_ticked_cards_and_says_where_they_landed`) are
about the auto-stage-move lifecycle, not prompts: both reproduce byte-identically in a
pristine `git worktree` at pre-card HEAD, i.e. committed before this card and untouched by
its hunks.

    QT_QPA_PLATFORM=offscreen build/relay-board-tests \
      theExecuteTaskCarriesTheBoardsConventions theExecuteTaskAsksForTheImplementedByTrailer \
      theBriefsAskForTheExactModelAndTheGuestHarness executeHandsTheCardToAPaneAndMovesItToExecuting
    Totals: 6 passed, 0 failed — the pinned executeTask wording holds.

    ctest --test-dir build -R '^board'
    boardsections, boardworkspace, boardpane: Passed. board: 28 passed, 1 failed.

The one board failure is `theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives` — a
QFATAL test-function timeout at 300 s. That test exists in no commit (`git log -S` finds
nothing): it is another session's uncommitted GUI work sitting in the shared checkout. This
card's hunks touch only `executeTask`'s string building and the three `theExecuteTask*`
assertion regions; the delete/undo test exercises neither.

## Live

The change is prompt text the worker imports at runtime from `backend/relay_core` (and the
`executeTask` string in the built binary), so the runs above exercised the exact landed
bytes. What only a live round trip adds is a real model *writing* an Orchestration block and
Execute starting the planned subagents together — that is the verifier's checklist item.
