# Evidence — #YJ4A: the claim chip on a card row links to its pane

Commit under test: `bbcf3b438801` ("board: the claim chip on a card row links to the pane that
claimed it (#YJ4A)"), parent `9ddfbc8c`.

## What the change is

In the Board cards list, a card row whose claim chip (⧉ xxxxxxxx, #R9G7) names a pane that is
still open now acts as a link:

- `RowDelegate::sessionChipRectOf` — the placed `Badge::Session` rect from `filteredShape`,
  exactly where `paint()` draws it; empty when the pane has closed or `fitBadges` dropped the
  badge on a narrow row.
- `RowList` — `sessionChipAt`/`sessionChipUnder` hit-testing with the one-gesture guards in
  `mousePressEvent` / `mouseReleaseEvent` / `mouseDoubleClickEvent` (beside the id-copy and label
  guards), a `mouseMoveEvent` pointing-hand cursor over a live chip, and an `onRevealPane(cardId)`
  callback wired in `buildChrome` to `revealClaim(card->session)` — the card page chip's own path
  (#R9G7).
- The claimed-by row tooltip now says "Click the chip to open it" while the pane is open; the
  closed line is unchanged.
- `BoardView::claimChipRect(cardId)` exposes the same rect so the test clicks what the guard
  hit-tests.

## How it was verified

`tests/boardmodel_test.cpp` — `BoardModelTests::aClaimedCardsRowLinksItsChipToThePane`:

1. A claimed executing card and an unclaimed card; `claimChipRect` is valid for the first and
   empty for the second.
2. `QTest::mouseClick` on the chip rect: `onFocusPane` receives the claiming pane's token, and
   nothing is sent (no card opened — the click is the chip's alone).
3. With `paneExists` returning false: `claimChipRect` is empty and the same click reveals
   nothing — a dead chip is inert paint.

Run on a **clean export of the exact landed tree** (not the shared working tree, which holds
other sessions' uncommitted edits):

```
$ git archive bbcf3b43 | tar -x -C <scratch>
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build --target relay-board-tests
$ ctest --test-dir build -R "^board$" --output-on-failure
Test project ...
100% tests passed, 0 tests failed out of 1     (board, 3.11s)
```

Also passed before landing: `ctest -R '^board$|^boardsections$'` on the shared build and in the
`land.py try` verify slot (`^board$|^boardsections$`), and `land.py commit`'s own build gate
(configure + `relay` target) on the exact landing tree.

The one `boardpane` failure seen during development was #HKY4's own uncommitted test
(`middleAndCtrlClickDockTheCardInItsOwnPane`), part of that session's in-flight restructure; this
change does not touch it.
