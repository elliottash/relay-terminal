# The Switchboard's sort menu — implementer evidence (#SEDZ)

The Switchboard list can now be ordered by time. A `Sort` menu beside the filter box offers
**Manual** (the rank drags write; Done and Verified stay newest first, as they always were),
**Newest first**, **Oldest first** (by `created`) and **Recently updated** (by the row's new
`updated` field — protocol 19.2 — the later of the card file's and its thread file's mtime; an
older worker sends none and that sort falls back to `created`). A time sort takes over every
section alike and switches the manual reorder off: a drop inside the card's own section and
Alt+Shift+↑↓ answer with a notice, while drops between sections still move. The choice is saved
with the window's layout and each pane restores its own.

`drive.sh` is the capture, following the recipe of
`docs/qa_evidence/2026-09-19-usage-meter-words/drive.sh`: Xvfb, an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` under a short path, `RELAY_KEYRING=off`. The
fixture board holds four cards whose rank, `created` and mtime orders all disagree, so every
sort is a different visible permutation:

| card | status | created | rank | mtime |
|---|---|---|---|---|
| Alpha reflow fix | ready | 2026-09-01 | c | now |
| Bravo voice mode | ready | 2026-09-10 | a | 5 days ago |
| Charlie theme swap | ready | 2026-09-16 | b | 9 days ago |
| Delta quick add | inbox | 2026-09-18 | a | 1 day ago |

Nothing is taken on trust by eye: every claim is read back out of the screenshots with tesseract
word boxes (`ocr.txt` is the receipt — the three orders, the button's label, the menu's entries,
the refusal notice).

## The shots

| file | what it shows |
|---|---|
| `01-board-open.png` | the Switchboard with the toolbar: `Sort: Manual` beside the filter, `+ New card`, `Clean up` |
| `02-manual.png` | **Manual**: rank order `Bravo, Charlie, Alpha` (ocr: `Bravo Charlie Alpha`) |
| `03-menu-open.png` | the menu open: `Manual (drag order)`, `Newest first`, `Oldest first`, `Recently updated` |
| `04-newest.png` | **Newest first**: `Charlie, Bravo, Alpha`; the button now reads `Sort: Newest first` |
| `05-menu-again.png` | the menu open a second time, ticked on `Newest first` |
| `06-updated.png` | **Recently updated**: `Alpha, Bravo, Charlie` — the touched-now card on top whatever its `created` |
| `07-notice.png` | Alt+Shift+↓ on a card under a time sort: `The list is sorted by recently updated, so reordering is off. Sort by Manual to drag cards around.` |
| `08-narrow.png` | a 620 px window: the sort button drops to the wrapped row with `+ New card` and `Clean up`, out of the filter's way |
| `08-narrow-tools.png` | the same, cropped to the tools at 2× |
| `ocr.txt` | the run's receipts, one line per claim |

## The unit tests

- `tests/boardmodel_test.cpp`: `timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip` (the four
  orders, the Done/Verified rule, the id round-trip, unknown → Manual) and
  `theSortMenuOrdersTheListAndReordersOnlyOnManual` (the button, the reorder of the rows, the
  Alt+Shift+↑↓ refusal with no write under a time sort, the write back on Manual).
- `tests/test_board_protocol.py`: `test_a_row_says_when_the_card_last_changed` — the row's
  `updated` is an ISO UTC timestamp and moves when the thread file is written.

Full suites on the shared tree: `ctest` and `./scripts/test.sh` pass except two faults that are
not this card's — `buttonfit` (dark-copper `font-size: 8.5pt` under the 9pt floor, noted on
#DKCV) and a `test_board_turns` flake that passes 3/3 in isolation.
