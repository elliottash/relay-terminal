---
id: FKSN
type: work
status: planned
labels: [feature, switchboard]
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: pane 1, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Board: sortable "Viewed" column to get back to recently-viewed cards

## Issue
add a "Viewed" column on the board that i can sort by, so i can easily geto cards that i was just looking at but didnt update anything

## Done means
The board list page shows a fifth sortable header column, **Viewed**, next to Updated. Opening a card's page (from the list, a `#ID` link, or a solo/pinned pane) stamps that card locally with the time it was viewed — per machine, never written into the card file or git. Clicking the Viewed header sorts the list most-recently-viewed first, clicking again flips to least-recently-viewed, a third click returns to the manual order — the same cycle the Created and Updated columns already have, with the arrow on the active column. Cards never viewed sit at the far end under the viewed sorts and show a blank cell. It did not work if: the stamps do not survive a restart, so the sort regresses to Manual or forgets timestamps after reopening Relay; or if the stamp leaks into a card file or the worker (a `git status` in the board workspace must stay clean after viewing cards).

## Plan
**Goal.** A fifth sortable column, **Viewed**, on the board list: it shows when you last opened each card's page and sorts by it (most recent first), so a card you were just reading but did not change is one header click away — the ask in `## Issue`.

**Findings.**

- Column and sort plumbing lives in `src/BoardModel.h` / `src/BoardModel.cpp`: `SortColumn { Priority, Card, Created, Updated }`, the `Sort` enum, and helpers `columnTitle`, `nextColumnSort`, `sortColumnIndex`, `sortAscending`, `sortId`/`sortFromId` (the pane's layout node keeps the sort id; `sortFromId` maps unknown ids to `Sort::Manual`). The comparator (`sortCompare`/`sortTime`, `src/BoardModel.cpp` ~1560) compares on `sortTime(card.created)/sortTime(card.updated)` with rank + path as tie-break; `Model::sorted` applies it.
- The header row is built in `BoardView` (`src/BoardPane.cpp` ~1130) from a fixed column list; `cardShape` (~983) measures the date-column rects used by row painting and the too-narrow drop rule; `board::dateCell` (~354) formats a timestamp as a relative date for painting.
- `Card` already declares `unread` "local, from QSettings; never in git" — but **nothing ever sets it**: there is no local-state mechanism yet, so this card adds the first one (a small QSettings-backed map, same contract as `unread`).
- Opening a card page funnels through one place: `BoardView::openSelected()` (`src/BoardPane.cpp` 6954), reached from Enter/double-click and from `openCardFromClick` → `openCard` (7313); solo panes and `#ID` chip links go through `openCardSolo` → the same path. That funnel is where a viewed stamp belongs — no worker round-trip, no file write.
- Existing sort tests: `tests/boardmodel_test.cpp`.

**Steps.**

1. `src/BoardModel.h`: add `QString viewed;` to `Card` (comment: local, from QSettings, never in git — beside `unread`), `SortColumn::Viewed`, `Sort::RecentlyViewed` and `Sort::OldestViewed`, and extend the helpers: `columnTitle` → "Viewed", `nextColumnSort` (RecentlyViewed → OldestViewed → Manual), `sortColumnIndex` → 4, `sortAscending`, and `sortId`/`sortFromId` with ids `"viewed"`/`"viewed-oldest"`.
2. Add the local store: a QSettings-backed map card id → ISO-8601 last-viewed stamp, keyed per board root so two workspaces cannot collide (same group scheme any existing QSettings use in the board code follows). Give `Model`/`BoardView` a `setViewed(id, stamp)` that updates the in-memory `Card`, repaints the row's cell, and persists; loaded cards read their stamp from it. ISO stamps sort lexicographically like the other date sorts.
3. Stamp on open: in `BoardView::openSelected()`, write `now()` for `m_selected` through that store (and in the `openCard` solo/link path if it bypasses `openSelected` — check, it should not). Nothing is sent to the worker; nothing is written under `.board/`.
4. Comparator: extend `sortTime`/`sortCompare` with the two viewed sorts reading `card.viewed`; tie-break stays rank then path. Cards with an empty stamp sort as oldest under RecentlyViewed (and first under OldestViewed) so they never hide viewed cards.
5. List UI (`src/BoardPane.cpp`): add the Viewed column to the header column list (after Updated) with its object name and click handling from the same code the Created/Updated headers use; extend `cardShape`'s date columns with a viewed rect (subject to the existing too-narrow drop rule); paint the cell from `card.viewed` via `board::dateCell`, blank when never viewed; header tooltip "when you last opened this card".
6. Tests in `tests/boardmodel_test.cpp`: the new sorts order by viewed stamp, empty stamps sit at the expected end, tie-break holds, `sortId`/`sortFromId` round-trip the new ids and map garbage to Manual. Build through `scripts/relay-build`, run `ctest --test-dir build -R boardmodel`.

**Risks.**

- List width: a fifth column squeezes the others; the existing too-narrow drop rule should be checked against the narrowest realistic pane — if Viewed is always dropped there, the column is useless and its position (or hiding Created on narrow panes) is an owner decision.
- Multi-machine: the stamp is local on purpose (it answers "what was I just looking at"), so two machines disagree — accepted; say so in the tooltip if it needs wording.

**Verify.** `ctest --test-dir build -R boardmodel`; then live under Xvfb: open a board pane, open two cards, click Viewed — they sort most-recent-first, arrow shows, order survives restarting Relay with the same layout; `git status` in the board workspace stays clean after viewing.
