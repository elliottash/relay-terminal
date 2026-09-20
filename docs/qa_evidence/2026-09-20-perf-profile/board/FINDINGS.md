# Switchboard and side panes — performance findings (#PF4K, 2026-09-20)

Written by the board profiler (Opus subagent); saved by the orchestrator, which also confirmed
finding 1's two code facts (`src/BoardWorker.cpp:10,27`, `backend/relay_core/board_protocol.py:1082`)
by reading them. Numbers and harness notes: [`measurements.txt`](measurements.txt); scripts in
[`harness/`](harness/); trimmed perf reports and two screenshots in [`raw/`](raw/).

**Machines.** spark: aarch64, 20 cores, Qt 5.15.13, load 0.8–2.1. sphinxpad: i7-1365U, on AC,
governor `performance`, load 0.4–1.0; `~/relay-perf/build` picked **Qt5**, `build-qt6` is what the
`.deb` ships. Clean export of `main` at ccb31a8e, RelWithDebInfo. Data: the real board (337 cards /
170 threads) plus a synthetic 3,000-card board (`relay-board.py check`: 0 errors), and a read-only
copy of the owner's conversation store (863 conversations, 58,863 entries, 115 MB `index.db`).

## Ranked findings

### 1. The Switchboard stops loading entirely at about 1,160 cards, silently

Reproduced at 1,200 and 3,000 cards on spark and on sphinxpad with *both* Qt5 and Qt6: the pane
shows "Loading the Switchboard…" forever. `board_open` is one JSON line
(`backend/relay_core/board_protocol.py:1138`); `src/BoardWorker.cpp:10` caps the read buffer at
8 MiB and `:27` **kills the worker** past it. The payload measures 2,431,294 B at 337 cards,
8,596,942 B at 1,200 and 21,634,664 B at 3,000 — 7,211 B per card, so 8 MiB falls at about 1,163
cards. `MAX_ROW_TEXT` is 64 KiB per card, so about 128 fat cards are enough. The error goes to
`statusBar()->showMessage` (`src/RelayWindow.h:5390`), which this layout never shows.

Fix: chunk the cards (the pane already patches rows via `board_changed`), and/or fix finding 2.
Gain: the feature exists above 1,100 cards. Risk: low.

### 2. 92.6 % of the payload is full-card search text

`board_protocol.py:1082` puts each card's whole body and thread on every row; the GUI uses it in one
substring test (`src/BoardModel.cpp:1446`). `board.open` at 337 cards: 457 ms wall / 160 ms GUI CPU
(spark), 508 / 170 (sphinxpad Qt5), 632 / 240 (sphinxpad Qt6).

Fix: a `board_search` request over the snapshot the worker already holds, or ship a short digest.
Gain: 7,215 → about 535 B per card; open about 457 → 300 ms; removes the cliff in finding 1.
Risk: medium (filter semantics; `tests/boardpane_test.cpp` first).

### 3. Filter keystrokes scan 2.2 MB on the GUI thread

Per key: 30–50 ms spark, 20–40 ms sphinxpad Qt5, **60–80 ms sphinxpad Qt6**. Decisive control: a
`status:`-scoped term (returns before `card.text`) costs 0–10 ms; `zzzz` costs 20–40 ms, same pane,
same moment. Expanding the 239-card section changes nothing, so item rebuild is not the cost.
`perf`: about 22 % in the unexported libQt5Core case-insensitive search cluster. Root:
`Model::matches`, `src/BoardModel.cpp:1404/1446`.

Fix: cache a case-folded copy per row (about half back, one line); then restrict the scan to the
previous match set when the term grows; finding 2 removes it entirely. Risk: low.

### 4. Sessions search-as-you-type: 100–190 ms GUI CPU per key

300–900 ms wall on the real store, while the pane prints "40 ms" for its query. A no-match query
costs 10–20 ms; a 100-match one 140–160 ms. `perf` shows `QUnicodeTools::initCharAttributes`,
`QTextLine::layout_helper`, harfbuzz — **no SQLite frames**. Root: `RowDelegate` builds a fresh
`QTextDocument` and calls `setHtml` per row twice, in `sizeHint` (`src/Conversations.cpp:448-451`)
and in `paint` (`:486-488`), for all ~100 rows on every `rebuildTree` (`:1287`).

Fix: cache the laid-out document per (html, width), cleared on refill; keep the row height in item
data. Gain: about 4× (to 30–40 ms). Risk: low–medium (bound the cache; colour already comes from
`PaintContext`, so it is cache-safe).

### 5. A 15,830-line file freezes the GUI for 1,965 ms cold / 688 ms warm, +58 MB RSS

The same bytes named `.txt` (no syntax definition matches) take **169 ms / 130 ms CPU** — so
KSyntaxHighlighting is about 520 of the 650 ms warm. `src/FilePanes.cpp:1312/1316/1320` (also
`:1130/1134`, `:1646/1650`): `setDefinition` rehighlights all 15,830 blocks synchronously for about
40 visible ones.

Fix: install the highlighter after first paint and rehighlight in chunks from the top on idle; skip
highlighting above about 2 MB. Gain: first paint 1,965 → about 200 ms. Risk: low–medium (cancel on
document replace — matters for the editor pane).

### 6. Every `board_refresh` parses the board twice, whatever changed

179 ms spark / 158 ms sphinxpad at 337 cards; 1,596 / 1,440 ms at 3,000 — identical whether one card
changed or none. `_changed` → `_rows()` (`board_protocol.py:1110`) re-reads everything, then
`_problems()` (`:1125`) runs `board.check()`, a *second* full parse (72 of the 179 ms). A 60 s storm
at one write per second: GUI 880 ms (1.4 % of a core), worker 3,830 ms (6.2 %).

Fix: (a) split `Board.check()` (`backend/relay_core/board.py:1178`) so `_problems` reuses the parsed
cards — about 40 % off immediately; (b) stat-cache `_rows()` and re-parse only changed paths.
Gain: 179 → about 15 ms; 1,596 → about 60 ms. Risk: low / medium.

## Qt5 vs Qt6 on the laptop (same C++)

Open 508 → 632 ms; filter keystroke 20–40 → **60–80 ms** (2.5×); expand unchanged; 1,200 and 3,000
cards broken on both. Verify any fix to finding 3 on the Qt6 build.

## Measured and fine — do not re-profile

- `relay-board.py`: `check` 0.10 / 0.70 s at 337 / 3,000 cards on spark, 0.10 / 0.63 s on sphinxpad;
  linear. **There is no PyYAML**: `board.py:435 parse_yaml` is hand-written and 4.8 % of the profile.
  The only smell is `pathlib.relative_to` at 1.17 s of `index`'s 2.26 s cProfile (28,504 calls,
  mostly `category_of`, `board.py:1109`), worth about 0.1 s and only while touching that code.
- `RowList` is a `QListWidget` plus a delegate, one item per row, **no per-card QWidget**
  (`src/BoardPane.cpp:902/402`); expanding 239 cards = 119 ms / 20 ms CPU. Reorder 55 ms / 10 ms.
- Scrolling everywhere: PageDown 0–10 ms, Ctrl+End 20 ms, 20 wheel clicks 60 ms.
- Watcher: 12 inotify watches, 11 of them the board's 11 directories, capped at 200, 400 ms debounce.
- A card with a 42-entry thread opens in 200 ms / 120 ms CPU. Sessions SQL: 2–40 ms.
- Models pane: 140–180 ms, but `perf` is flat (QCss selector matching, widget construction) — no
  catalog parse, no fetch. Caching the built pane would buy about 150 ms, the smallest item here.

## Incidental, not performance

The watcher watches *directories*, so an append to an existing card or thread often does not fire:
only about 21 of 60 writes reached the pane in the storm. Correctness, not performance.

## Read, not touched

#SDXE (pane jiggle) is already landed in needs-verification (`b2461d52`); its cause is header-chrome
`minimumSizeHint` following content, unrelated to the board refresh path. No Switchboard width change
was seen across any refresh, including the storm.

## Could not measure

Every *interactive* figure at 3,000 cards, because finding 1 stops the pane loading (the worker-side
scaling at that size is measured directly and reported above). And a named symbol for the Qt5
string-search cluster — no Qt debug symbols on either machine, so it is identified by position plus
the scoped-vs-plain control, which is the stronger evidence anyway.

## Harness gotchas worth keeping (detail in `measurements.txt`)

- An isolated `XDG_RUNTIME_DIR` kills the pane agent (code 1) unless `/run/user/$UID/bus` is
  symlinked in.
- A fresh profile opens the welcome dialog and Approvals over the pane under test: set
  `instructions/onboarded` and `security/approvals_chosen`.
- No window manager on Xvfb, so `windowfocus --sync`, not `windowactivate`.
- XWD region hashing needs `bytes_per_line` = header word 12, with pixels at
  `header_size + ncolors*12`; getting this wrong makes an idle window appear to repaint at 50 Hz,
  which it does not. sphinxpad has no `xwd`; `import -window <id> xwd:-` substitutes.
