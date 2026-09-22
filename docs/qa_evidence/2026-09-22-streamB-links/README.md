<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The terminal view's link painter, before and after — #NK73, 2026-09-22

Evidence for `c4a657ed`, which fixes finding 11 of
[`../2026-09-22-phone-ux-drive/`](../2026-09-22-phone-ux-drive/README.md) and the six smaller
faults on the card. Everything here is a measurement of `app/screen.js`, taken twice: once at
`c4a657ed^` and once at `c4a657ed`.

## What was wrong, in one table

`rows-before-after.txt` is the same nine rows through the old painter and the new one, printed by
`paint_rows.mjs`:

```sh
git show c4a657ed^:app/screen.js > /tmp/screen-before.js
node docs/qa_evidence/2026-09-22-streamB-links/paint_rows.mjs /tmp/screen-before.js app/screen.js
```

`linkify()` is pure — runs in, pieces out — so it runs outside a browser, which is what makes a
side-by-side table possible at all. Before:

```
in : "see https://a.example.com and https://b.example.com now"
out: "see https://a.example.comsee https://a.example.com  now"   <-- NOT THE ROW
     link "https://a.example.com" -> https://a.example.com
     link "ttps://a.example.com " -> https://b.example.com       <-- the label is not the address
```

and the same row after:

```
out: "see https://a.example.com and https://b.example.com now"
     link "https://a.example.com" -> https://a.example.com
     link "https://b.example.com" -> https://b.example.com
```

The table also holds a case the card did not have: a URL **split across two coloured runs**, which
is what any ANSI-coloured line is. Before, `"see https://a.example.com and https://b.example.com
now"` in two runs painted as `"see https://a.example.commple.com and https://b.exa now"` — the
halves of the row swapped places. The same slicing bug; it needed no second URL, only a second
run, so an `npm ERR!` line in red was enough.

The rest of the table, before → after:

| row | before | after |
|---|---|---|
| `banner: WWW.EXAMPLE.COM is the site` | href `WWW.EXAMPLE.COM`, **relative** — the client's own origin | `https://WWW.EXAMPLE.COM` |
| `run curl "https://api.example.com/v1" twice` | `…/v1"` → `…/v1%22` | `https://api.example.com/v1` |
| `see <https://example.com/a> in the RFC` | `…/a>` → `…/a%3E` | `https://example.com/a` |
| `path /var/www.old/index.html is served` | `https://www.old/index.html`, a live link to nothing | not a link |
| `open https://…/signed/path` on a 47-column screen | linked to the prefix it can see | not a link |
| `https://example.com/x_(1), ok` | right already | unchanged |

## The tests, run against the code that had the bug

`tests/test_web_screen.py` gained five tests. `before.log` is them against `c4a657ed^` — a clean
export of the parent commit with only the new test file copied in, so nothing of the fix is in the
tree:

```sh
git archive c4a657ed^ | tar -x -C /tmp/nk73-before
cp tests/test_web_screen.py /tmp/nk73-before/tests/
cd /tmp/nk73-before && RELAY_KEYRING=off python3 -m unittest tests.test_web_screen.ScreenCostTests -v
```

```
Ran 9 tests in 10.9s
FAILED (failures=5)
```

The five failures are the five new tests; the four that were already there pass, which is the
point — the old painter satisfied every assertion anybody had written about it. What each says
before the fix:

| test | how it failed at `c4a657ed^` |
|---|---|
| `…painted_exactly_as_it_was_sent` | `'see https://a.example.comsee https://a.example.com  now' != 'see https://a.example.com and https://b.example.com now'` |
| `…not_linked_to_its_prefix` | the wrapped row's URL was linked to `https://example.com/a/very/long/signed/path` |
| `…keeps_its_colour_and_a_concealed_one_stays_concealed` | the anchor's colour was `''`, and the concealed row painted `token https://secret.example.com/t` in full and tappable |
| `…font_buttons_work…when_storage_throws` | `A+ did not move the floor at all: 12 != 16` |
| `…cursor_stays_in_view…` | `the cursor was left off the right edge: 0 not greater than 0` |

`after.log` is the whole module in the checkout at `c4a657ed`: **10 tests, OK**, in 16 s. That
includes the two cost tests of #3H5T, unchanged — the point of doing the sideways follow by
arithmetic is that `report.style` and `report.clientWidth` are still 0 through 200 frames, and the
cursor test asserts the same two counters over a cursor walking all 80 columns.

## The two measured numbers

* **Sideways follow.** The bench is 400 px wide with 80 columns, so at the 12 px floor a cell is
  7.22 px, the grid is 610 px and the wrap shows 358 px of it — the same shape as the phone the
  card measured. With the cursor at column 70 the cell begins 506 px into the grid: at
  `scrollLeft: 0`, which is where it always stayed, that is ~155 px past the right edge and the
  reader is typing blind. It now sits at `scrollLeft: 169` with the cursor cell's rectangle inside
  the wrap's (`cell.right` 360 against `box.right` 400). Dragging up into the scrollback and
  typing again leaves `scrollLeft` where the reader put it: the follow hangs off `toBottom()`, so
  it never moves anybody who is reading back.
* **A−/A+ with storage throwing** (`localStorage` redefined to throw, which is iOS private
  browsing, a sandboxed iframe and `file://`): two A+ now give a 16 px grid that survives a
  `refit()`. Before, the floor read back as 12 every time and the grid never moved at all.

## Not changed, and why

* **`app/style.css`.** Nothing here needed a rule: `#screen-wrap` was already `overflow: auto`,
  and `a.screen-link { color: inherit }` is exactly what lets the anchor take the run's colour
  from the span inside it. Another session owns that file today.
* **The cursor cell on a *coloured* link.** `.screen-row .cursor` colours the cell, and an inline
  colour on the span inside beats it — which is already true of every coloured run in the grid,
  and is now true of a coloured link too rather than links being the one exception. Making the
  cursor cell win over a run's own colour is a style.css decision about all runs, not about links.
