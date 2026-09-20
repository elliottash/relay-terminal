# The Switchboard's priority flag, live (card #VKFV)

`drive.sh` builds a board of four fixture cards (Ready: no flag / +3 / +1, Inbox: −1; labels
`feature gui`, `feature voice`, `bug`, `bug gui`; created dates and mtimes spread over three
weeks), runs `build/relay` on it under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, and drives it with xdotool. Every claim is checked by OCR
(tesseract word boxes on a 2x upscale of the board pane — the coordinates map back to
full-image space for the clicks) or by reading the fixture card files. `ocr.txt` is the
receipt; the PNGs carry what OCR cannot (the colours, the ring, the ⚑).

Run: `bash drive.sh [build-dir] [out-dir]` (defaults: `../../../../../build`, this folder).

## What was verified (ocr.txt lines in brackets)

- **The `#ID` is a fixed second column before the title** — the leftmost word on a row before
  the title is the `#id`, ~48 px of id column ahead of the title's first word [01].
- **No age badge** — zero `N d`/`N w` tokens on the page; the row's dates are the two real
  columns, Created and Updated [01].
- **The header is ⚑ CARD CREATED UPDATED** — the glyph is not OCR-readable, the words are [01].
- **Left click raises, right click lowers** — one left click took Charlie +1 → +2 with the
  notice "Flagged #PJQJ at +2" and `priority: 2` in the card file; a right click took it back
  to +1 with its own notice [02, 03]. The clicks clamp at ±3/−1 (unit-tested too).
- **The ⚑ sorts** — priority high first: +3, +1, 0 (Bravo Charlie Alpha); low first:
  0, +1, +3 (Alpha Charlie Bravo); a third click is Manual again (rank a,b,c) [04–06].
- **The label chips filter** — `bug feature gui voice` under the section checkboxes [01];
  ticking `bug` left only the bug cards and switched the count to "1 shown" [07]; unticking
  brought the board back [08]. (Inbox stayed folded in this run, so its bug card is the unit
  test's to prove: `labelChipsKeepOnlyTheCardsThatCarryThem`.)
- **The colours are the theme's** — pixels sampled from `06-manual.png` at each row's flag:
  +3 `srgb(126,200,140)` = the bright green `#7ec88c`; +1 `srgb(236,230,224)` ≈ the white
  `#e6e8ec` antialiased on the face; 0 a hollow ring (face colour at the centre, dim brass at
  the stroke). The −1 yellow disc was not on screen (the −1 card is the folded Inbox one);
  the token is `#e5c07b` and `theme_test` holds all four to 3:1 on the face.

## The crash this run found and fixed

The first pass segfaulted on the very first flag click: `onPriority(row->cardId, …)` binds a
reference into `m_rows`, and `setCardPriority` rebuilds the list — replacing `m_rows` — before
using the id. The section handlers copy their id by value for exactly this reason; the flag
handler now does too (`setCardPriority(QString id, int)`). `relay.log` in this folder is the
crashing run's; the final `ocr.txt` is from the fixed build.
