---
id: FT77
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: kimi/kimi-k3
session: e273439f-004b-4369-b20d-b81d11a21ec0
rank: zzzzzzzzzzzzzzzzr
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-switchboard-ref-copy-button/], related: [3ZAP, Y2F4], github: null}
---
# A ⧉ copy button beside a card's #ID — in the rows and on the card page

## Issue
in the switchboard, in the issue rows @/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260920-202014.png  or the card headers @/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260920-202029.png 

put the copy icon button next to it, you click on that and it will copy the hash tag

## Execution Summary
A ⧉ copy button beside a card's `#ID`, on both surfaces the screenshots named.

- **The card rows** (`src/BoardPane.cpp`): `CardShape` carries an `idCopyRect` straight after the id's glyphs; `paintCard` draws the ⧉ muted, in the row's ink under the pointer. The id column widened by the glyph's room (`idCopyWidth`) so every row's ⧉ lines up like every row's id, the column header over the list moved with it, and the date columns still give way first. A click copies `#ID` through `copyTag` — clipboard, the "Copied #ID" notice, the "#ID copied" toast (#Y2F4) — and never selects the row: press, release and double-click are guarded by the same one-gesture hit-test the label badges keep (#3ZAP).
- **The card page's header**: a ⧉ `QToolButton` (`boardCardRefCopy`) beside the ref, styled like the ref (muted, ink on hover, `src/Theme.cpp`), doing the same copy.
- Both raise the one-off hint "Next time: y" — `y` copies the selected card's reference and the click is the slow path (WARP.md hint rule); the trigger is in `docs/ARCHITECTURE.md`'s registry, which also names the button beside the toast paragraph.
- `tests/boardmodel_test.cpp`: `theRefCopyButtonCopiesTheIdInTheRowsAndTheHeader`. The #3ZAP test's plain-row click moved 40 px right, onto the title — the id column is the button's now.
- `docs/qa_evidence/2026-09-21-switchboard-ref-copy-button/`: the Xvfb drive, screenshots, `ocr.txt`.

Nothing about the reference behaviour changed: a `#ID` in the card's words still zooms, no copy.

## Tasks
- [x] Row: idCopyRect + painted ⧉ + one-gesture hit-test, id column widened <!-- t:b4 -->
- [x] Card page: ⧉ button beside the ref, styled, same copy <!-- t:z2 -->
- [x] copyTag path: clipboard + notice + toast for both <!-- t:sz -->
- [x] Shortcut hint "Next time: y" on both clicks; registry entry <!-- t:b7 -->
- [x] Unit test + the #3ZAP test's plain-row click moved onto the title <!-- t:xq -->
- [x] Live Xvfb drive 25/25, evidence dir <!-- t:dk -->
- [x] Land through scripts/land.py, my hunks only — 19df94e6d3a1 <!-- t:mz -->


## Tests
- `./build/relay-board-tests theRefCopyButtonCopiesTheIdInTheRowsAndTheHeader` — PASS (header button: clipboard, notice, toast, the `y` hint; row ⧉: sweep finds it, copies without selecting).
- The whole board suite on this checkout: **97 passed, 0 failed** (`./build/relay-board-tests`, offscreen).
- `docs/qa_evidence/2026-09-21-switchboard-ref-copy-button/drive.sh` under Xvfb: **25 passed, 0 failed** — the row's ⧉ and the header's ⧉ each copy `#DC4J` with notice and toast and without opening the card; the #3ZAP/#Y2F4 surfaces still pass; a `#KAN3` reference still zooms with the clipboard untouched.

## QA checklist
- [ ] Re-run the drive on a quiet tree: `docs/qa_evidence/2026-09-21-switchboard-ref-copy-button/drive.sh` — 25/25, and read `ocr.txt` rather than trusting the count.
- [ ] By hand, on a card row: the ⧉ sits right after the `#ID`, muted, and lights up with the row under the pointer. A click puts `#ID` on the clipboard, shows "Copied #ID" in the notice and "#ID copied" at the pane's bottom-right — and does **not** select or open the row. A click on the id's own text or the title still selects, as before.
- [ ] By hand, on the card page: the ⧉ beside the `#ID` in the header does the same copy. Tooltip says what it is.
- [ ] Every row's ⧉ lines up in one column; the list's column header still sits over the titles; the date columns still appear and disappear as the pane resizes.
- [ ] The first click (per surface, per the hint gates) shows the one-off "Next time: y" hint; `y` still copies the selected card's reference.
- [ ] Unchanged: label badges copy their `#tag`; a `#ID` reference in the card's words or the thread zooms and leaves the clipboard alone.
