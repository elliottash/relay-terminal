# #7M6E — the Switchboard's payload, its cliff and its refresh (before / after)

Measured on **spark** (aarch64, 20 cores, Qt 5.15.13, Python 3.12), 2026-09-20.

* **before** = the clean export of `main` the #PF4K profilers used, `/tmp/claude-1000/pf4k/src`.
* **after** = this working tree.
* Both against two copies of the same board files, so neither warms the other's page cache first:
  the repo's own board (**353 cards**, 186 threads) and a **3,000-card** synthetic board built with
  the profiler's `synth_board.py` (`relay-board.py check`: 0 errors).
* Harness: [`measure.py`](measure.py) — it drives `backend/worker.py` over stdin exactly as the GUI
  does (`configure`, `board_open`, `board_refresh` ×3 best-of, `board_search` ×3 best-of), follows
  the `more` flag to collect every `board_cards` batch, and reports the **biggest single message**,
  which is what the GUI's read buffer caps.

```
python3 measure.py <backend source root> <workspace with a board in it>
```

## 353 cards (the repo's own board)

| | before | after |
|---|---|---|
| `board_open` | 309 ms | **204 ms** |
| `board` payload | 2.51 MB, one message | **0.23 MB**, one message |
| bytes per card | 7,107 | **653** |
| of which `text` | 92.5 % (6,573 B/card) | — (not sent) |
| `board_refresh`, nothing changed | 192 ms | **6 ms** |
| `board_refresh`, one card touched | 190 ms | **6 ms** |
| `board_search` "zzzz" / "composer" / "switchboard pane" | — (no such message) | **0.5 / 1.1 / 0.9 ms**, 136 / 652 / 1,036 B |
| worker RSS after open | 48 MB | 38 MB |

The card's targets were ~535 B/card, open ~300 ms and refresh ~15 ms. Bytes per card land at 653
rather than 535 — the remainder is the row's own fields (`path` 75 B, `title` 67 B,
`implemented_by` 34 B, `updated` 22 B), which the pane draws — and refresh beats its target.

## 3,000 cards (synthetic)

| | before | after |
|---|---|---|
| `board_open` | 2,073 ms | **1,204 ms** |
| `board` payload | 21.20 MB | **1.97 MB** over 8 batches |
| **biggest single message** | **21.20 MB** — over the GUI's 8 MiB cap, worker killed | **0.27 MB** |
| `board_refresh`, nothing changed | 1,639 ms | **55 ms** |
| `board_refresh`, one card touched | 1,620 ms | **58 ms** |
| `board_search` (3 queries) | — | **4.7–4.9 ms**, 257–7,781 B |
| worker RSS after open | 175 MB | 85 MB |

The cliff is gone by construction: `MAX_ROWS_PER_MESSAGE` (400) and `MAX_ROW_BYTES_PER_MESSAGE`
(512 KiB) cap every message, so no board size can reach `kMaxBuffer` in `src/BoardWorker.cpp`.

## What is left on the GUI thread per keystroke (the Qt6 question)

Finding 3 measured 30–50 ms a keystroke on spark and **60–80 ms on sphinxpad's Qt6 build**, and the
control that identified it was a `status:`-scoped term, which returned before `card.text` and cost
0–10 ms. What the filter now does on the GUI thread **is that control**: `Model::matches` runs the
scoped terms against the row's fields and, for the plain words, one `QSet<QString>::contains` on the
card id — no `card.text`, because no row carries it. The 2.2 MB case-insensitive scan that `perf`
attributed 22 % of the profile to is not reachable from the filter any more. The list rebuild
(`refill`) is unchanged and was measured as not the cost (expanding a 239-card section changed
nothing). The worker-side search is 0.5–1.1 ms at 353 cards and under 5 ms at 3,000, off the GUI
thread and debounced by 120 ms, so a burst of typing is one question.

The orchestrator re-measures on sphinxpad with both Qt builds.

## Tests

Added, each failing before the change and passing after:

* `tests/test_board_protocol.py` — `board_search` semantics pinned against what `Model::matches`
  did with `text` on the row (body, thread, author, kind; the row's own fields; case-insensitive;
  every word must match; entry-header metadata excluded; follows the cards as they change), the
  searchable-text cap, `board_open` batching (`cards_total`, `more`, every card once, every
  message inside the cap, a small board still one message), and the parse counts behind finding 6
  (`board.Card.load` / `board.parse_thread` patched and counted): a refresh with nothing changed
  parses **0** files, one changed card parses **1 card, 0 threads**, one thread append parses
  **1 card, 1 thread**, a `board.yaml` change re-parses everything, and the problems a refresh
  reports are byte-identical to a full `board.check()`.
* `tests/boardmodel_test.cpp` — `plainTerms`, and `matches` with and without the worker's id set
  (the scoped terms compose with it; an all-scoped filter is not hidden by an empty set; the
  model uses an answer only while it answers the words in the box).
* `tests/boardfilter_test.cpp` (new target `boardfilter`) — one `board_search` per burst of
  typing, none at all for an all-scoped filter, a superseded answer dropped, a re-ask when the
  cards change; `board_cards` batches patched in; a `board_worker_status` before any `board` event
  replacing "Loading the Switchboard…" and its Retry sending `board_open`.

Run: `ctest --test-dir build -R 'board|boardfilter|boardpane'`, and
`PYTHONPATH=backend:tests python3 -m unittest tests.test_board_protocol tests.test_board`.
Two failures in `tests/test_board_protocol.py` (`ProbeAndImportTests.test_apply_creates_…`,
`WriteTests.test_a_card_detail_read_round_trips_…`) are **pre-existing on main** — they fail
identically on the clean export — and are card #VASY ("three board tests fail on main").

## #TZWF item 4, landed here because it is the same file

`backend/relay_core/board_protocol.py` imported `board_import`, `forge_github`, `forge_sync` and
`project_probe` at module level for five on-demand messages. They are imported inside the handlers
now. Isolated best-of-9 in this tree:

```
from relay_core import board_protocol                                          40.2 ms
from relay_core import board_protocol + the four modules                       46.2 ms
```

**6.0 ms per worker start**, against the card's estimated 5.5 ms.
