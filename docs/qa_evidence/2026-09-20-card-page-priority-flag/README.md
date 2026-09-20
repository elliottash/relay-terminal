# The priority flag on the card page, live (card #DPJB)

`drive.sh` builds a board of three fixture cards (Ready: Alpha unflagged, Bravo +2; Inbox:
Charlie −1), runs `build/relay` on it under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, and drives it with xdotool: `Ctrl+Shift+S`, unfold Ready, select a
row, `Enter` to open the card page, then click the flag at the head of the page's header. Every
claim is checked by reading the fixture card file after the click, by OCR of the page (tesseract on
a 2x/3x upscale, boxes mapped back to full-image space for the clicks) or by sampling the pixels of
the flag itself. `ocr.txt` is the receipt; the PNGs carry the colours.

Run: `bash drive.sh [build-dir] [out-dir]` (defaults: `../../../../../build`, this folder).

## What was verified (ocr.txt lines in brackets)

- **The card page has the flag.** The detail's header reads `| @ #9ZAH #ID → prompt (t) Open file
  (o) × |`: the `@` tesseract sees at the head of the line is the priority disc, left of the `#ID`
  label — the row's own flag, on the page the card is read on [01].
- **A left click raises it, and the write is real.** Bravo opened at +2 (a pale-green disc,
  `srgb(176,214,178)`); one left click at the flag (x=744, 16 px left of the id label) wrote
  `priority: 3` to `issues/features/…bravo-voice-mode.md`, repainted the disc in the bright green
  `srgb(126,200,140)` = the theme's `#7ec88c`, and left the pane's own notice `Flagged #9ZAH at +3`
  plus the worker's `owner flagged this card · priority +3` entry on the card [02].
- **It clamps at +3.** A second left click at +3 left the file at `priority: 3` [02b].
- **A right click lowers it, one step at a time, through the whole range.** +3 → +2
  (`srgb(176,214,178)`, pale green) → +1 (`srgb(236,230,224)`, the theme's white `#e6e8ec`) → 0
  (`priority:` gone from the file, and the box is only the face plus the dim brass stroke —
  `srgb(66,46,33)`/`srgb(48,33,26)`: the empty ring) → −1 (`srgb(229,192,123)` = the yellow
  `#e5c07b`) [03]. The notices name the value each time: `Cleared the flag on #9ZAH` at 0, and the
  worker logged six priority entries on the card by the end [03].
- **Ctrl+Z undoes the page's click**, like any other write: after the −1, `Ctrl+Z` left the file
  with no `priority` key at all [06].
- **One flag, two places.** Clicking Alpha's flag *in the list* (left = +1) wrote `priority: 1`;
  opening Alpha's page showed the same flag at the head of the header with the +1 disc
  (`srgb(236,230,224)`) — and a right click on the *page* cleared it again [05, 05b]. The row and
  the page are one write path (`BoardView::setCardPriority` → `board_priority`).

## Unit tests added (both green on the implementer's tree)

- `tests/boardpane_test.cpp` — `theCardPagesFlagClicksThroughToBoardPriority`: opens a card in a
  `BoardView`, presses the header flag left (one `board_priority` for the card, priority 1, and the
  page's own tooltip turns to `+1` at once), then right three times (0, −1, and −1 again at the
  clamp). `ctest --test-dir build -R boardpane` → 5 passed.
- `tests/test_board_tools.py` — `SpecTests.test_the_update_tool_says_the_priority_flag_is_settable`:
  `board_update_card`'s description names `priority` and that 0 clears the flag.
  `python3 -m unittest tests.test_board_tools.SpecTests tests.test_board_tools.PriorityTests` → ok.
