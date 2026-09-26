# #YN4D — the Board list's Stage column is filled for every card

Landed in `e6c96b59` on `main` (tip `10e6151f` + the #YN4D hunks only).

## What was wrong

Two separate causes, both behind "the stage column is missing for most cards":

1. **Painting (the main one, flat list included).** `CardDelegate::paint` in `src/BoardPane.cpp`
   drew the stage pill at `shape.stageRect`, which is relative to the row, without
   `.translated(origin)` (the date cells beside it were translated). Every row painted its pill at
   the viewport's top, over the first row, so only the top card ever showed a stage. This was not
   in the card's plan; it turned up in the screenshots (`flat-*` before the fix showed a single
   INBOX pill for six cards).
2. **Model.** `Model::rows()` set `Row::stage` only in the flat branch, so in sections grouping
   there was no stage to draw. The sections branch now sets it (with the flat branch's special
   case for Verified) and no longer sets the multi-status `showStatus` badge the pill replaces.
   `cardShape` still turns the stage into a status badge when the row is too narrow for the column.

## Evidence

- `ctest-board.txt` — `board` and `boardpane` pass on the landed tree, built in a `land.py`
  verify slot. The commit's own verify ran the same two tests on tree `213cbe4ec441`.
- `board-tests-stage-cases.txt`: the two model tests that changed, `theRowListIsHeadersThenCards`
  (sections rows carry `Needs QA (LLM)`, `Ready to start` and no `showStatus`) and
  `aFlatListIsEveryShownCardInOneOrder` (every sections card row has a stage; header rows none).
- New test `everyCardRowPaintsItsOwnStagePill` grabs the list's viewport in sections and flat
  grouping and requires ink in each card row's own Stage cell.
  `negative-control-untranslated-pill.txt`: with only the `.translated(origin)` reverted, on a
  clean `git archive` of `e6c96b59`, it fails (`sections AAA1`).
- Screenshots from `shot-harness.cpp`: a real `relay::BoardView` with six cards over the seven
  statuses, offscreen, linked against `relay-board` built from the working tree with this change:
  - `sections-1100.png`, `sections-700.png`: every row has its pill, including both Needs QA
    rows (`NEEDS QA (LLM)`, `NEEDS QA (HU…)`).
  - `flat-1100.png`, `flat-700.png`: every row has its pill.
  - `sections-580.png`, `flat-580.png`: the column no longer fits, and each row shows the stage as
    a status badge in the stage's ink (`Inbox`, `Discu…`, `LLM QA`).

## Not changed

- At about 520 px and below, the title's minimum width takes the room, and `board::fitBadges`
  drops even the fallback badge. That was already true of the flat list before this change
  (#ESDF: "a narrow pane loses decoration before it loses meaning").
- The STAGE header cell stays visible at every width because it is the grouping toggle.
