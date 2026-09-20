# The Switchboard's column header — implementer evidence (#Z2VT)

The list's sort is no longer a `Sort:` button beside the filter. It is the list's own column
header (owner, 2026-09-19: "change switchboard sorting from a sort button to adding header columns
that you click on"), three cells over the rows — **Card**, **Created**, **Updated** — each one a
click that orders the cards *inside every section* ("and sorting is within section"):

| column | first click | second click | third click |
|---|---|---|---|
| Card | Title A→Z | Title Z→A | the board's own order |
| Created | Newest first | Oldest first | the board's own order |
| Updated | Recently updated | Least recently updated | the board's own order |

The cell that is on wears the arrow (`▲` for oldest first, A→Z and least recently updated; `▼` for
the other way) and the accent, and its tooltip names the orders a click walks through, so the way
back to the drag order is written where the click happens. **Created** and **Updated** are also
drawn as the row's two right-hand columns (owner: "add a 'created' and 'updated' column"), in mono
at the right of the badges; they and their labels go together when the pane is too narrow to carry
them. The sort still rides the window's layout (`{"board": {…, "sort"}}`), now with
`"updated-oldest"`, `"title"` and `"title-desc"` beside the four ids it had.

The section list's own order is the gear's business (owner: "we do need sorting of sections though.
enable those to be dragged and dropped, with up and down buttons for moving them, in the section
settings modal"): every row of the section page has a drag handle and `▲ ▼` buttons, a drop lands
above or below the row it is dropped on, and moving a section rewrites `columns:` in the new order
with no card touched. Verified and Done are always the last two — they are not draggable, their
buttons are off, and nothing moves past them.

`drive.sh` is the capture, following the recipe of
`docs/qa_evidence/2026-09-19-switchboard-sort/drive.sh`: Xvfb, an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` under a short path, `RELAY_KEYRING=off`. The
fixture board holds four cards whose rank, `created` and mtime orders all disagree, so every click
is a different visible permutation:

| card | status | created | rank | mtime |
|---|---|---|---|---|
| Alpha reflow fix | ready | 2026-09-01 | c | now |
| Bravo voice mode | ready | 2026-09-10 | a | 5 days ago |
| Charlie theme swap | ready | 2026-09-16 | b | 9 days ago |
| Delta quick add | inbox | 2026-09-18 | a | 1 day ago |

Nothing is taken on trust by eye: every claim is read back out of the screenshots with tesseract
word boxes (`ocr.txt` is the receipt — the header's own words and arrow, the three orders, the
dates in the two cells, the refusal notice, the section page's rows, and the date columns' absence
in a narrow pane). The two glyphs OCR cannot read — the `▲ ▼` buttons and the drag handle — are
clicked by position (the gear is found by sweeping the wrapped checkbox row, past every checkbox so
a stray click can only land on it) and are left to the unit tests for their behaviour.

## The shots

| file | what it shows |
|---|---|
| `01-board-open.png` | the Switchboard with the header row over the list: `CARD CREATED UPDATED` (ocr), and no `Sort:` control anywhere (ocr counts 0) |
| `02-manual.png` | **Manual**: the board's own rank order `Bravo, Charlie, Alpha` |
| `03-created-newest.png` | a click on **CREATED**: `Charlie, Bravo, Alpha`, and the arrow on that cell (`CARD CREATED ▼ UPDATED` in ocr) |
| `04-created-oldest.png` | a second click: `Alpha, Bravo, Charlie`, the arrow turned (`▲`) |
| `05-updated.png` | a click on **UPDATED**: `Alpha, Bravo, Charlie` — the file touched now on top whatever its `created`, arrow moved to that cell |
| `06-notice.png` | Alt+Shift+↓ on a card under a column sort: `The list is sorted by recently updated, so reordering is off. Click that column's header again for the board's own order.` |
| `07-sections.png` | the gear's page: one row per section with a drag handle and `▲ ▼` beside `Merge…` and `✕`; `Inbox … Done` in order, `Nothing changed yet.`, `Cancel` / `Save sections` |
| `09-narrow.png` | a 560 px window: no `CREATED`/`UPDATED` label and no date cell left (ocr counts 0 of each) |
| `09-narrow-header.png` | the same, cropped to the list's top at 2× |
| `ocr.txt` | the run's receipts, one line per claim |

The two date cells are readable in the shots: the rows ocr as

```
Alpha reflow fix  #…  2w  2026-09-01  2026-09-19
Bravo voice mode  #…  9d  2026-09-10  2026-09-14
Charlie theme swap #… 3d  2026-09-16  2026-09-10
```

— the card's `created` and the later of its file's and its thread file's mtime, which is what the
**Updated** column sorts on.

## The unit tests

- `tests/boardmodel_test.cpp`
  - `timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip` — the seven orders (including
    `OldestUpdated`, `TitleAsc`, `TitleDesc`), the Done/Verified rule, the id round-trip, unknown →
    Manual.
  - `theColumnHeaderSortsTheListWithinASection` — the header's three cells, no `boardSort` button
    left, the click → order → arrow → cycle back to Manual, a click on another column starting that
    column's cycle, the Alt+Shift+↑↓ refusal with no write under a column sort, and the write back
    on Manual.
  - `theColumnsNameTheOrdersAClickGoesThrough` — the pure mapping (`nextColumnSort`,
    `sortColumnIndex`, `sortAscending`) and `dateCell`.
- `tests/boardsections_test.cpp`
  - `movingASectionRewritesColumnsAndNeverACard` — `move` and `moveBefore`, `columns:` in the drawn
    order, the refusals at both ends and on the fixed two, a drop under Verified landing above it,
    and a section that only exists because a card carries that status moving without being written.
  - `theRowsMoveWithTheButtonsAndTheDropLandsWhereItWasDropped` — the page: a handle and `▲ ▼` per
    row, the fixed two not draggable, `▲` on Ready reordering the rows and the footer saying
    `moved above`, a drop on a row's top half landing above it and on its bottom half below, and one
    `board_sections` message on Save.

Run: `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R board` (board, boardsections,
boardworkspace — all pass), plus `-R "theme|buttonfit|hints"` for the stylesheet the header cells
are coloured by (all pass).
