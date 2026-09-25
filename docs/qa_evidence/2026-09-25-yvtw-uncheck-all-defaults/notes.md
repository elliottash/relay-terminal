# #YVTW — Uncheck all, closed stages unticked by default, no Active/Deferred sections

Implemented in commit `4a4df89650aa` (landed 2026-09-25 via `scripts/land.py`, verify slot
`relay-terminal-1006c7a3-0`: the exact landed tree builds the `relay` target).

## What was changed

- `src/BoardModel.cpp` — `Model::sections()`'s extras no longer draw a section for memory cards
  (any memory status) or for `deferred` (`earnsNoSection`); `Model::openCount()` skips them too,
  so the count label's "N of M open" only counts cards the list can draw.
- `src/BoardPane.cpp` — `BoardView` seeds `m_hidden` with `verified`, `done`, `dropped`
  (a saved choice still replaces the default); the checks row carries an **Uncheck all** button
  (`boardSectionUncheckAll`); `BoardView::selectCard` ticks a reached card's section back on and
  unfolds it when the card is not on the page.

## Test evidence (2026-09-25, build/)

```
$ cd build && ./relay-board-tests 2>/dev/null | tail -2
Totals: 96 passed, 0 failed, 0 skipped, 0 blacklisted, 1396ms
$ ./relay-boardpane-tests 2>/dev/null | tail -2
Totals: 20 passed, 0 failed, 0 skipped, 0 blacklisted, 386ms
$ ./relay-boardsections-tests 2>/dev/null | tail -2
Totals: 19 passed, 0 failed, 0 skipped, 0 blacklisted, 99ms
```

New test: `theClosedStagesStartUntickedAndUncheckAllClearsTheRow` (defaults, the button, the
honest count, one-tick recovery). Updated for the new default: `closedCardsGoToTheDoneSection…`,
`memoriesKeepTheirOwnStatuses`, `aSectionCheckboxTakesItsSectionOffThePage…`,
`aSelfClosedCardReachedByIdUnfoldsItsGroup`, `theFoldRowToggles…`, `theViewRendersOneList…`,
and in boardsections_test the two examples that used a deferred extra card (now `icebox`).

## Known consequence to be aware of

A work card whose status is `deferred` no longer appears on the cards list (owner's ask:
"remove active and deferred"); it is still on the board and reachable to agents. If that proves
too invisible, say the word and Deferred can fold into the Done section instead.
