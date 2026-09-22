---
id: NK73
type: work
status: inbox
labels: [bug, remote]
assignee: null
rank: znk73
created: '2026-09-22'
source: 'Measured by Claude Code painting rows through the real ScreenView, 2026-09-22'
links: {plans: [], commits: [88ba42ab, f919f14b], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [PH0N], github: null}
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
