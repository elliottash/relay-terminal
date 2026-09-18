# `#K7Q2` in terminal output opens its card (owner 2026-09-18)

Design: [`docs/SWITCHBOARD-DESIGN.md`](../../SWITCHBOARD-DESIGN.md) §5 ("Links in the output").
Layer: [`docs/ARCHITECTURE.md`](../../ARCHITECTURE.md) "Clickable paths in terminal output".

**Not a QA verdict.** Produced by the implementing model, Claude Opus 5 (1M context), under Xvfb
with an isolated profile. A QA session from a different model family should check it.

## How the screenshots were made

`drive.sh [build-dir]` — one fresh Relay on `:184`, 1400×880, against a **throwaway copy** of this
repository's `issues/` tree under `/tmp` (the real one is never opened, let alone written; the copy
is deleted when the run ends).

The shell prints the line under test from a file the script writes, so no `#` is ever typed into
the composer — there, `#` opens the card picker instead (design §5):

```
recap: #YZTK shipped; #ABCD was never filed   # YZTK is the real one
#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK,#YZTK
```

`#YZTK` is a real card in the copied tree; `#ABCD` was never filed. The second line repeats the
reference so that five characters in six anywhere along it belong to one, which is what lets the
script hover it without knowing the engine's cell geometry: it walks the pointer down the rows and
watches for the hover underline, comparing only the row under the pointer so that cursors blinking
elsewhere cannot be mistaken for it (`hover-scan.txt` is that search; the underline appears on one
row and nowhere else).

**No model call is made.** Nothing is ever submitted to the agent. The provider is a preset with a
dummy key from the environment (`RELAY_KEYRING=off` keeps the run away from the real keyring) and
every proxy variable points at a dead loopback port, so even an accidental request could not leave
the machine. The session has to be *configured* only because that is what lets the pane ask the
worker for its card index, which is what decides whether a `#K7Q2` is a link at all.

## Files

| file | what it shows |
|---|---|
| `01b-hover-underline-detail.png` | **the one to look at first**: the hovered `#YZTK` underlined, while `#ABCD` and the `# YZTK …` shell comment on the line above stay plain text |
| `00-output-with-references.png` | the pane before any hover |
| `01-hover-underline.png` | the same hover, whole window (taken before the tooltip pops, which is a window of its own) |
| `02-hover-tooltip.png` | the tooltip: `#YZTK · Clickable file and folder paths open Relay panes` — the id *and* the card's title, from the pane's own index |
| `03-right-click-menu.png` | the right-click menu on the reference: `Open #YZTK "Clickable file and folder paths open Relay panes"`, `Copy #YZTK`, `#YZTK → prompt`, above the pane's usual entries |
| `04-reference-in-the-prompt.png` | after `#YZTK → prompt`: `#YZTK` in the composer, styled as a reference |
| `05-keyboard-walk.png` | Ctrl+Shift+L three times: `14 of 16 · #YZTK · Clickable file and folder paths open Relay panes · Enter opens, Esc leaves`, with that one reference selected and underlined. 16 links: 13 references, the `./recap.sh` path and the two prompt directories |
| `06-clicked-card-open.png` | one plain click on the reference: the Switchboard opens in **this** tab and the card is open in it, on `#YZTK` |
| `07-ctrl-clicked-from-inactive-pane.png` | the same reference Ctrl+clicked once the Switchboard has the focus (a plain click there is the click that moves the focus, so it is disarmed) |
| `hover-scan.txt`, `hover-y.txt` | the underline search and the row it settled on |
| `relay-stderr.log` | empty on a clean run |

## What is not covered here

- The detection rules themselves are unit tests, not screenshots:
  `tests/outputlinks_test.cpp` (known id links, unknown id does not, a `#` comment does not, ids at
  a line's start and end and inside punctuation, `#1234`, `#TODO`, a URL fragment, lower case) and
  `tests/backends_test.cpp` (the three menu entries).
- A pane that has never seen a board: the unit tests cover it (`scan()` with no `CardLookup` links
  nothing), and in the app the pane asks for the index the first time a reference-shaped span is
  scanned, so the link appears from the next hover on.
- One display, one theme, one font size.
