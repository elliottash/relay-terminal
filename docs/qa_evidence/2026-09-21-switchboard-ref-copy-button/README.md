# A ⧉ beside a card's `#ID` copies the reference (#FT77)

Owner request, 2026-09-20, with screenshots of a `#DC4J` row and a card page's header: *"in the
switchboard, in the issue rows … or the card headers … put the copy icon button next to it, you
click on that and it will copy the hash tag"*.

## What was built

- **The card rows.** Every row's `#ID` now carries a ⧉ right after the id's glyphs, muted as the
  id is and in the row's ink under the pointer. A click on it copies `#ID` to the clipboard, with
  the board's "Copied #ID" notice and the "#ID copied" toast (#Y2F4) — and it never selects the
  row: press, release and double-click are guarded by the same one-gesture hit-test the label
  badges keep (#3ZAP). The id column widened by the glyph's room, so every row's ⧉ lines up like
  every row's id, and the column header over the list moved with it.
- **The card page's header.** A ⧉ `QToolButton` beside the ref, the sign a copyable id wears in
  the info pane (#YQC3), doing the same copy.
- Both raise the one-off shortcut hint "Next time: y" — `y` copies the selected card's reference,
  and the click is the slow path (the RELAY.md hint rule); the registry in `docs/ARCHITECTURE.md`
  names the trigger.

## What was checked

`drive.sh` re-runs #3ZAP's/#Y2F4's surfaces and clicks the two new buttons, on a fixture whose ids
are pinned to the owner's own screenshots: the reference is `#KAN3`, the card under test `#DC4J`.

    docs/qa_evidence/2026-09-21-switchboard-ref-copy-button/drive.sh [build-dir] [out-dir]

## Result — 25 passed, 0 failed

| Step | Surface | Clipboard | Notice | Toast |
| --- | --- | --- | --- | --- |
| 02b | the row's ⧉ beside `#DC4J` | `#DC4J` | "Copied #…" | "#DC4J copied" |
| 04b | the card page header's ⧉ | `#DC4J` | "Copied #…" | "#DC4J copied" |
| 02, 04–06 | the #3ZAP surfaces | `#bug` | "Copied #bug" | "#bug copied" |
| 06b | 2.4 s later | — | still up | **faded** |
| 07 | a `#KAN3` reference | untouched | — | — (it zoomed) |

Both new clicks also left the board exactly as it was: the row's click did not open the card, and
the reference click still zooms with the clipboard untouched. Screenshots and the raw pass/fail
lines are beside this file (`ocr.txt`).

Unit test: `theRefCopyButtonCopiesTheIdInTheRowsAndTheHeader` (`tests/boardmodel_test.cpp`) — the
header button's copy with notice, toast and hint; the row's ⧉ found by a sweep over the id column,
copying without selecting the row. The board suite is 97/97 on this checkout.

## Caveats

- Run on the shared checkout, whose `src/BoardPane.cpp` also carries another session's uncommitted
  work (the `Del` delete key hints, the `kCardSplitWidth` split floor, the check-strip verdict
  truncation). The landed hunks are this change's alone.
- OCR reads `#DC4J` loosely — `#0C4J`, `#0c4]` — and merges the painted ⧉ into the id's token
  (`#0C4Jo0`). The row click therefore goes a fixed 16 px left of the title's column (measured on
  the shot: the glyph's centre), and the notice is checked by its "Copied #" wording, with the
  exact id proven by the clipboard and the toast.
- A display is free only when nobody's socket file *and* no Xvfb process is on it: a server whose
  socket file was deleted still holds the display, and the first run died on exactly that.
