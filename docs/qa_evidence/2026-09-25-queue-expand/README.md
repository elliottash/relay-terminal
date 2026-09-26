# Card #JDN4 — a queue line opens to its whole message (implementer evidence)

Commits: `669be841` (state, hub cleaner, phone view, protocol doc) and `736a9e76` (desktop
strip, tests).

## Screenshots

Desktop screenshots come from `tests/jdn4_queue_expand_cases.h` (ctest `queueexpand`). It runs a
real console pane, offscreen, with a stub context: a 120-word prompt is running and a 300-word
(>2,000-character) prompt is queued behind it. The screenshots were written with
`RELAY_JDN4_EVIDENCE_DIR` set during `land.py try jdn4 --tests '^(queueexpand|panestate|queuecontract)$'`.

- `collapsed.png` — both lines folded to one, each with `▾`. The queued row keeps `→` and `×`.
- `expanded.png` — both open. The running line wraps in a scroll area limited to a quarter of
  the strip. The queued row wraps in place and its lane scrolls by pixel. `▴`, `→` and `×` stay
  on the first line.

Phone screenshots come from `app/pane-demo.html` in headless Chrome, 390×760 touch viewport. The
`busy_queue` fixture was used, with row `entry:7` given a 330-word `full` ending `\nTHE REAL END`
and a two-line running `full`.

- `phone-collapsed.png` — folded; `▾` only on lines whose text overflows or carries a `full`.
- `phone-expanded.png` — both open, text wrapped (`white-space: pre-wrap`), other rows unchanged.
- `phone-expanded-end.png` — the open row and the list scrolled to the end: `word329` then
  `THE REAL END`, not `…`. The rows below it are unchanged.

## What the tests assert

- `panestate` (`tests/panestate_test.cpp`): `full` is absent for short rows. For a
  2,000-character row it ends with the real last words while `label` stops at 400 with `…`.
  It keeps line breaks and is capped at 16,000. `running.full` behaves the same.
- `tests/test_remote_pane_state.py::CleanTests::test_a_rows_whole_text_survives_with_its_line_breaks`:
  the hub keeps `full`, redacts a key inside it, caps it, and drops it when it equals the label.
- `tests/test_pane_view.py::PaneViewTests::test_a_long_queue_line_opens_to_its_whole_text_and_stays_open`:
  ▾ shows the whole text. It does not change the selection, open the sheet or send anything.
  Five 10 Hz state ticks leave it open, ▴ folds it, and tapping the row body still selects it.
- `queueexpand`: ▾ on the desktop row opens it (taller than four lines, ends with the real
  words) without selecting or removing it. The running toggle does the same. A `queue_changed`
  event plus a resize, which rebuilds the strip, keeps both open. `remoteState()` publishes both
  `full` texts. ▴ folds both back to one line, and × still removes the row.

## Known failures outside this change

`consolemode` (default suite) fails at `consolemode_test.cpp:726-739` (link open path) and at
`:2010/2013` (turn-summary spacing). Both are recorded as failing on `main` (card #DJ3X, #6BY7
thread). `tests/test_pane_view.py::OutboxTests` errors on a Node ESM import of `app/outbox.js`,
which this change does not touch.
