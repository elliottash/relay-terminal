---
id: NK73
type: work
status: needs-verification
labels: [bug, remote]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: znk73
created: '2026-09-22'
source: Measured by Claude Code painting rows through the real ScreenView, 2026-09-22
links: {plans: [], commits: [88ba42ab, f919f14b, c4a657ed, 28ab38c9], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamB-links/], related: [PH0N], github: null}
---
# Two URLs on one terminal row make the phone print text the terminal never sent

## Issue
`linkify()` (`app/screen.js:29-72`) tracks an absolute cursor `start` across the row's segments but
slices each piece with offsets computed against `start` instead of against the segment's own origin
`at`. With one URL per segment `start === at` on entry, so it works and the tests pass; with a
second URL the offsets are wrong. Painted through the real `ScreenView` at 390×844
(`docs/qa_evidence/2026-09-22-phone-ux-drive/G-two-links-on-a-row.png`):

```
in : 'see https://a.example.com and https://b.example.com now'
out: 'see https://a.example.comsee https://a.example.com  now'
     link 'https://a.example.com'   -> https://a.example.com/
     link 'ttps://a.example.com '   -> https://b.example.com/      <-- label is not on the row

in : 'https://a.example.com https://b.example.com https://c.example.com'
out: 'https://a.example.comhttps://a.example.com https://a.example.com '
```

The word `and` is gone, the first URL is printed twice, and the second anchor's **visible text says
one address while its href is another** — a link that lies about where it goes, in a terminal. Two
links on a line is ordinary output: `git remote -v`, an `npm audit` advisory, a `curl -v` trace.
Knock-on: the painted pieces no longer sum to the row's length, so `column += text.length`
(`:645`, `:655`) drifts and the cursor cell lands in the wrong column on that row.
`tests/test_web_screen.py:107` paints exactly one URL per row, live and in scrollback, which is why
this shipped.

Four smaller faults in the same painter, all measured in the same run:

| row | href produced |
|---|---|
| `banner: WWW.EXAMPLE.COM is the site` | `http://<relay-host>/…/WWW.EXAMPLE.COM` — a **relative** href into the client's own origin, because `URL_RE` is `/…/gi` but the scheme test is a case-sensitive `url.startsWith('www.')` (`:53`) |
| `run curl "https://api.example.com/v1" twice` | `…/v1%22` — `trimUrl` (`:29-44`) strips `.,;:!?` and unbalanced brackets, not quotes |
| `see <https://example.com/a> in the RFC` | `…/a%3E` — nor angle brackets |
| `path /var/www.old/index.html is served` | `https://www.old/index.html` — the `www.` alternative has no left-hand boundary, so a path becomes a live link to a host that does not exist |

And two more found reading the file:

* **A wrapped URL links to a truncated address.** The header comment at `:22-24` says "a URL the
  terminal wrapped across two rows does not link". It does: the first row's fragment still matches
  `https?://[^\s]+`, so at 80 columns a long PR or signed-S3 link is underlined and tappable and
  opens the prefix — a 404, or a silently wrong signed request. Not linking it would be safe.
* **Linked text loses its colour and its attributes.** `link()` (`:668`) calls
  `this.span(text, 0, 0, 0)`, discarding the `fg, bg, attrs` that `linkify` carefully carried onto
  every piece. A URL inside an ANSI-red error line paints in the default foreground; one inside a
  reverse-video run loses the inversion and paints dark on dark; and one under `ATTR.CONCEAL` —
  which `span` would blank — is printed in full **and** made tappable, so a deliberately hidden
  tokenised URL is revealed on the phone but not on the desktop.

Also in this file, from `f919f14b`: **A−/A+ are dead rather than session-only when `localStorage`
throws.** `fontFloor()` reads only from storage and `setFontFloor()` writes only to it
(`:255-270`), with no in-memory field, so the comment's promise ("private mode: the default stands,
and A−/A+ work for the session") cannot hold — `refit()` re-reads and gets 12 again. And **nothing
ever scrolls the grid horizontally**: `scrollLeft` appears nowhere in `screen.js`, `toBottom()`
sets `scrollTop` only. With Keyboard (direct keys) on at the 12 px floor, 80 columns is about
576 px of grid on a 358 px view, so the cursor leaves the right edge around column 30 and the
reader types blind until they drag.

## Done means
Every row is painted exactly as the terminal sent it, whatever it contains: the text of
the row's pieces concatenated equals the row's text, for one URL, two, or three. Every anchor's
visible text is the address it goes to. A URL keeps the colour and attributes of the run it came
from, a concealed one stays concealed, an uppercase `WWW.` gets an absolute href, quotes and angle
brackets stay out of the href, and a URL that runs to the last column is not linked to its prefix.
It fails if any row's painted text differs by one character from what was sent.

## Execution Summary
`linkify()` tracked an absolute cursor across a row's runs but sliced each piece against that
cursor instead of against the run's own origin `at`. With one URL per run the two are equal on
entry, so it worked and the tests passed; with two the offsets were wrong and the phone printed a
row the terminal never sent. Every slice is now taken against `at`, and the pieces sum to the row
by construction.

**The fault was wider than the card said.** Building the before/after table found a case that needs
no second URL, only a second *run*: a single URL split across two coloured runs painted
`"see https://a.example.commple.com and https://b.exa now"`. So any ANSI-coloured line with one URL
in it was enough to corrupt the row — not the two-URL line the card was filed on.

In the same painter: the scheme test is case-insensitive, so `WWW.EXAMPLE.COM` gets an absolute
href instead of a relative one into the client's own origin; `trimUrl` strips trailing quotes,
backticks and angle brackets as well as sentence punctuation, so `curl "…/v1"` and `<…/a>` no
longer carry `%22` and `%3E`; a bare `www.` must be preceded by whitespace or an opening bracket,
so `/var/www.old/index.html` is text again; a URL that runs to the row's last column is not linked
at all, because its href would be a truncated address — a 404 or a silently wrong signed request —
and the header comment now describes what the code does; and `link()` takes the run's
`fg, bg, attrs`, so a URL keeps its colour, bold and reverse-video, while a run under `CONCEAL` is
not linkified at all (it was printed in full *and* made tappable, revealing a hidden token on the
phone and nowhere else).

From the font work: `fontFloor()` keeps the reader's choice in a field written by `setFontFloor()`,
with `localStorage` only as the way it outlives the page — so where storage throws (iOS private
browsing, a sandboxed iframe, `file://`) A−/A+ work for the session instead of being dead. And
`toBottom()` keeps the cursor cell in view horizontally: only at the live end, never while somebody
is reading back, and by arithmetic off the last `fit()`, so a frame of output still costs no layout
read — the #3H5T cost assertions are unchanged and still pass.

`app/style.css` needed no change: `#screen-wrap` was already `overflow: auto`, and
`a.screen-link { color: inherit }` is what lets the anchor take the span's colour. One thing was
deliberately not changed and is recorded in the evidence README instead: the cursor cell drawn over
a *coloured* link takes the run's colour, because an inline colour beats `.screen-row .cursor`.
That is already true of every coloured run in the grid, so carrying colour into links makes links
behave like the rest; making the cursor win over a run's own colour is a decision about all runs
and belongs to whoever owns `app/style.css`.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_web_screen` — 10 tests, OK, 16 s (four that were
already there, five new). Re-run by the orchestrating session after the landing, not only by the
implementer.

- `tests/test_web_screen.py::ScreenCostTests::test_a_row_with_more_than_one_url_is_painted_exactly_as_it_was_sent`
- `tests/test_web_screen.py::ScreenCostTests::test_a_url_that_runs_to_the_last_column_is_not_linked_to_its_prefix`
- `tests/test_web_screen.py::ScreenCostTests::test_a_link_keeps_its_colour_and_a_concealed_one_stays_concealed`
- `tests/test_web_screen.py::ScreenCostTests::test_the_font_buttons_work_for_the_session_when_storage_throws`
- `tests/test_web_screen.py::ScreenCostTests::test_the_cursor_stays_in_view_on_a_grid_wider_than_the_screen`
- `manual: docs/qa_evidence/2026-09-22-streamB-links/` — `paint_rows.mjs` runs `linkify()` from any
  revision of `app/screen.js` outside a browser; `rows-before-after.txt` is the same nine rows
  through `c4a657ed^` and `c4a657ed` side by side, `before.log` is the five new tests failing
  against the parent (5 of 9) while the four older ones pass, `after.log` is 10/10.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the independent probe this card was filed
  from, re-run against the landed tree by the orchestrating session: **0 of 7 rows painted text the
  terminal never printed** (it was 2 of 7), `WWW.EXAMPLE.COM` now resolves to
  `https://www.example.com/` rather than a path on the client's own origin, the `%22` and `%3E`
  hrefs are gone, and `/var/www.old/index.html` is no longer a link.
