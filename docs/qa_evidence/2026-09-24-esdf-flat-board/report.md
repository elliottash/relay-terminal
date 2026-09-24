# #ESDF — flat board with a Stage column (desktop + phone)

Landed: `dc5b457f6189602f1012e8cb13b764b16256492c` (desktop Switchboard),
`3303f02a0951367fbb2ae31bf8f5f79e99df4beb` (phone board).

## Desktop

- Build: `scripts/relay-build` (clean), and land.py's verify build of the exact
  tree that went on `main` (`/tmp/claude-1000/land/esdf/verify/build`) — both exit 0.
- `ctest --test-dir <verify build> -R board`: `board`, `boardsections`, `boardsignals`,
  `boardpane`, `boardfilter`, `boardwatch` pass. `boardworkspace`, `boardremote`,
  `boardexecute` fail **identically on a clean export of the tip without this change**
  (`git archive 0d5b34e1` into `/tmp/esdf-tip`, same three failures) — pre-existing,
  filed as **#D9AQ**, not this card's work.
- New cases in `tests/boardmodel_test.cpp`:
  `aFlatListIsEveryShownCardInOneOrder` (flat order under each sort, no headers/folds,
  stage text per row, checkboxes/label chips/filter composing, `groupingId` round-trip,
  Manual follows global rank, sections unchanged) and
  `theStageHeaderTogglesTheFlatListAndTheChoiceIsKept` (new pane opens flat + recently
  updated, STAGE click back and forth, `boardHeaderStage` button and its accent,
  Alt+Shift+↑↓ reorders across stages in flat without changing status).

## Phone

- `tests/test_board_view.py` — all 34 cases pass
  (`PYTHONPATH=backend python3 -m unittest tests.test_board_view`).
- New case `test_recent_opens_as_one_list_newest_first_each_row_naming_its_stage`:
  the board opens as one list — every shown card of the fixture, newest first (ties by
  id), a stage chip on every row ("Discussing" on the newest); "Stages" brings the
  sections back in the desktop's order, `localStorage.relay-board-grouping` keeps the
  choice, "Recent" returns to the one list.
- `phone-390x844-board-recent.png` / `phone-390x844-board-stages.png` are that test's
  own screenshots of the two views.

## Not done here

- Drag-to-reorder and drag-onto-a-section in flat (desktop): a flat drag reorders rank
  only; a stage moves by Alt+Shift+←/→ or the card's menu, as the plan agreed.
