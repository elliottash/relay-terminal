---
id: 7M6E
type: work
status: needs-verification
labels: [bug, switchboard, performance]
assignee: claude-code
rank: m1
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [682e8f7a], evidence: [docs/qa_evidence/2026-09-20-perf-profile/, docs/qa_evidence/2026-09-20-perf-fixes/board/], related: [PF4K], github: null}
---
# The Switchboard stops loading above ~1,160 cards; full card text rides on every row

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Measured on spark and sphinxpad (Qt5 and Qt6), clean export of main at ccb31a8e. Detail, commands and perf reports: [docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md), findings 1, 2, 3 and 6.

1. **Silent failure at ~1,160 cards.** `board_open` is one JSON line (`backend/relay_core/board_protocol.py:1138`); `src/BoardWorker.cpp:10` caps the buffer at 8 MiB and `:27` kills the worker past it. 7,211 B per card: 2.4 MB at this board's 337 cards, 8.6 MB at 1,200. The error goes to `statusBar()->showMessage` (`src/RelayWindow.h:5390`), which this layout never shows, so the pane says "Loading the Switchboard…" forever. About 128 cards at `MAX_ROW_TEXT` (64 KiB) are enough.
2. **92.6 % of the payload is search text.** `board_protocol.py:1082` puts each card's body and thread on every row for one substring test (`src/BoardModel.cpp:1446`). Open at 337 cards: 457 ms / 160 ms GUI CPU (spark), 508 / 170 (sphinxpad Qt5), 632 / 240 (Qt6).
3. **Each filter keystroke scans 2.2 MB on the GUI thread** (`Model::matches`, `src/BoardModel.cpp:1404/1446`): 30–50 ms spark, 20–40 ms sphinxpad Qt5, 60–80 ms sphinxpad Qt6. A `status:`-scoped term, which returns before `card.text`, costs 0–10 ms.
4. **Every `board_refresh` parses the board twice whatever changed**: `_rows()` (`board_protocol.py:1110`) then `board.check()` in `_problems()` (`:1125`). 179 ms at 337 cards, 1,596 ms at 3,000.

## Plan
1. Search in the worker over the snapshot it already holds (a `board_search` request), or ship a short digest per row: ~7,215 → ~535 B per card, open ~457 → ~300 ms, and item 3 goes away. Check filter semantics against `tests/boardpane_test.cpp` first.
2. Chunk `board_open` so no board size can reach the cap, and show a worker failure in the pane, not the status bar.
3. Split `Board.check()` (`backend/relay_core/board.py:1178`) so `_problems` reuses the parsed cards (~40 % off), then stat-cache `_rows()` and re-parse only changed paths (179 → ~15 ms).
4. Verify on the Qt6 build: the filter is 2.5× slower there.

## What landed
1. **The rows stopped carrying each card's text.** `board_search {query}` (protocol 19.2) answers
   the filter's plain words in the worker, over a case-folded copy it keeps when it parses the
   card; the pane keeps the scoped terms (`status:`, `label:`, `folder:`, `@`, `#`) and answers
   them from the row. Debounced 120 ms, and only the newest request id is believed, so typing
   never queues a stale search. Until the answer for the words in the box arrives, the row's own
   fields decide — so a title match is instant and a body match joins it a moment later. The
   search is re-asked on every `board` and `board_changed`, so the filter follows the files.
   **7,107 → 653 B a card; `board` 2.51 → 0.23 MB at 353 cards.**
2. **`board_open` travels in batches** (`board_cards`, 400 rows / 512 KiB a message), and so do a
   `board_changed`'s upserts, so no board size reaches `BoardWorker`'s 8 MiB cap: the biggest
   message at 3,000 cards is 0.27 MB, where it was 21.2 MB. **A worker failure is now in the
   pane** — the line that said "Loading the Switchboard…" says what happened, with a Retry — and
   not only in a status bar this layout never shows.
3. **`Board.check()` was split** into `check_card`, `check_thread`, `check_thread_name` and
   `check_whole_board`, and the worker caches the per-file halves against each file's
   (mtime_ns, size), so `_problems()` reuses what `_rows()` parsed instead of parsing the tree a
   second time. A refresh with nothing changed parses **no** files. **179 → 6 ms at 353 cards,
   1,639 → 55 ms at 3,000.**

Numbers, harness and the Qt6 reasoning: [docs/qa_evidence/2026-09-20-perf-fixes/board/README.md](../../docs/qa_evidence/2026-09-20-perf-fixes/board/README.md).

## QA checklist
- [ ] Open the Switchboard on this repo's board. The cards appear and the count is right.
- [ ] Type a word that is only in a card's **body** (not its title) — e.g. `hotline` — and the
      card appears within a moment. Type a word only in a **thread** comment: same.
- [ ] Type a scoped term (`status:ready`, `label:bug`, `@agent`, `#7M6E`): filters instantly.
- [ ] Mix them (`status:ready composer`): both apply.
- [ ] Type quickly and then backspace: the list never shows the results of a query you have
      already typed past.
- [ ] With a filter in the box, edit a card file on disk so it starts (or stops) matching: the
      list follows within about a second.
- [ ] Build a 3,000-card board (`docs/qa_evidence/2026-09-20-perf-profile/board/harness/synth_board.py`)
      and open it: it loads, filters and scrolls. On `main` it never left "Loading the Switchboard…".
- [ ] Kill the Switchboard worker (`pkill -f 'worker.py'` with the pane open, or point the pane at
      a board whose worker cannot start): the pane says so where "Loading the Switchboard…" was,
      with a Retry that reloads it. A board already on screen keeps its cards and gets a notice.
- [ ] The problems banner still reports the same format problems (add a card with no front matter).
- [ ] Verify on the **Qt6** build: a filter keystroke was 60–80 ms there.
