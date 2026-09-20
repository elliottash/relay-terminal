---
id: 7M6E
type: work
status: executing
labels: [bug, switchboard, performance]
assignee: claude-code
rank: m1
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K], github: null}
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
