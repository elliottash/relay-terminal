# #FKSN — Board: sortable "Viewed" column

Implementer evidence, 2026-09-25.

## What changed

- `src/BoardModel.{h,cpp}`: `Card::viewed` (local only), `SortColumn::Viewed`,
  `Sort::RecentlyViewed` / `Sort::OldestViewed` (ids `viewed` / `viewed-oldest`), the header cycle
  (most recent → least recent → Manual), and `Model::setViewedStamps` / `setViewed`. Every card
  from the worker, whether it arrives in `reset` or in a later `upsert`, keeps its stamp. Cards
  never opened stay at the far end under both viewed sorts, and ties fall back to rank, then path.
- `src/BoardPane.{h,cpp}`: a fifth header cell `boardHeaderViewed`, rightmost after Updated, and a
  matching row cell painted with `board::dateCell`. It is the first right-hand column dropped
  when the pane narrows, so Created and Updated remain on every pane that showed them before.
  `BoardView::openSelected()` is the path every card-page open goes through: list Enter or click,
  a `#ID` link, and solo or pinned panes. It stamps the card with the current UTC time
  (ISO-8601, to the millisecond) in QSettings under `board/viewed/<hash of board root>`, keeping
  the newest 500 stamps per board. The stamp also goes to every open view of the same board, so
  a card opened in its own pane moves on the list it came from. Nothing is sent to the worker,
  and nothing is written under `.board/`.

## Tests (run in the land.py verify slot, on the exact tree that lands)

- `ctest -R '^board$'` (relay-board-tests): passed. New tests:
  - `viewedSortsOrderByLastOpenedAndNeverOpenedGoLast`: covers ordering both ways, cards never
    opened going last, a stamp surviving a worker upsert, and the tie-break.
  - `theViewedHeaderSortsByLastOpenedAndTheStampsSurviveAReopen`: covers the header click cycle
    and its arrow, a card opened in a second pane moving on the first, and the worker being sent
    only reads with no stamp. A pane opened afterwards (as after a restart) finds the stamps,
    and a different board keeps its own.
  - `theColumnsNameTheOrdersAClickGoesThrough` and `timeSortsOrderEverySectionAlike…` are
    extended with the new column, sorts and ids.
- `ctest -R '^boardpane$'`: passed.
- Both test binaries now set a test organisation in `initTestCase`
  (`QStandardPaths::setTestModeEnabled`), so their stamps go to `~/.qttest`, never to real
  settings.

## Screenshots (Xvfb, `RELAY_SHOT_DIR`)

- `board-top.png`: the header row with the fifth VIEWED cell after UPDATED.
- `board-viewed-sort.png`: the list sorted by Viewed (`VIEWED ▼`). The two opened cards come
  first with their dates, and the card never opened is last with a blank cell.
