# A markdown link's label opens what it names — card #MDKN

Implementer evidence for card #MDKN ("A markdown link's label is not clickable"), the open item
on #AGNT's QA list. Owner, 2026-09-21: **"yes, add the linking"**.

`drive.sh` runs it all under Xvfb against a stub provider on loopback — no provider account, no
network, `RELAY_KEYRING=off`, a private `HOME`/`XDG_*`/`TMPDIR` under a short path. **41 PASS ·
0 FAIL**, `notes.txt`.

```
docs/qa_evidence/2026-09-21-markdown-link-labels/drive.sh \
    /path/to/relay /path/to/out [phase ...]
# RELAY_QA_ON / RELAY_QA_OFF name the two builds the pixel phase compares
```

## What was built, in one paragraph

`[LABEL](target)` prints as `LABEL (target)`, the label in the link ink. Only the `(target)` was
clickable, because that is text and `relay::links` scans text. Now the label's cells carry an
OSC 8 run of their own — **the block's own prose anchor with the target as a fragment**,
`relay://prose/<pane>/<n>#l=<percent-encoded target>` (`src/LabelLinks.h`) — and the anchor is
re-opened straight after it, the same mid-line switch a tool-call row already makes for its
`#K7Q2` segment. `TerminalView::resolveLabelLink` asks `relay::links` what the fragment names, so
a label fills the same `Link` the printed target fills and opens by the same road,
`Pane::openOutputTarget`. Nothing in either emulator core changed, which is why this shape was
chosen over a private anchor mark or a second per-cell link layer: both of those live in the
cores, and only libvterm builds on this machine.

## What each phase clicks, and what it reads

Every gate reads something the transcript does *not* also say. An Options page is read out of
`RELAY_QA_RECTS` — the `settingsRowLabel`s that are actually drawn — so a page is proved by its
own rows being up and the other page's rows being gone, never by a word that occurs in the
answer. Every label is a word (`OPENROW`, `SESSIONROW`, `CARDROW`, `FILEROW`, `SITEROW`) that
appears nowhere else on screen, and the printed `(target)` beside it is a different token again,
which is what tells a click on the label from a click on the target.

| | what is clicked | shots | what it proves |
|---|---|---|---|
| **a** | `OPENROW` in a **terminal pane** | `a01-links`, `a02-options` | the label opens Options at `terminal/copy_on_select` — the page went General → Terminal, and General's rows are gone |
| **b1** | `SESSIONROW` in the **Sessions helper's console** | `b01-console`, `b02-links`, `b03-selected` | the context gets first refusal: the conversation is selected *in that pane* ("The pane header and its labels"), and the one selected before it is not |
| **b2** | `SESSIONROW` in a terminal pane | `b04-links`, `b05-sessions` | from a pane it goes through the window instead, and the Sessions pane comes up |
| **b-control** | the printed `(session:…)` **target**, same console | `bc2-links`, `bc3-selected` | the label and the target do the same thing on the one surface where a session link selects in place |
| **c** | `CARDROW` (`#ID`) in a terminal pane | `c01-links`, `c02-card` | the card page opens, on the card it named |
| **d** | `FILEROW` (a path), then `SITEROW` (an `https://` URL) | `d01-links`, `d02-file`, `d03-site` | the file opens in a preview pane; the web label resolves as a URL and is handed to the desktop, not read as a path that is not there |
| **e** | `OPENROW` in the **Options helper's own console** | `e01-console`, `e02-links`, `e03-revealed` | an embedded console: the row is revealed in that same pane |
| **f** | `OPENROW` after the pane is made narrower | `f01-wide`, `f02-narrow`, `f03-options` | the paragraph re-wraps from two rows to three — so the view is painting its own wrap of the block's logical lines, not the printed cells — and the label still opens the row. That is the `FoldSpan::link` road rather than the grid one |
| **g** | `OPENROW` inside a **thinking bubble** | `g01-thought`, `g02-unfolded`, `g03-options` | a fold's rows are spans the host draws; the label opens the row from there too |
| **h** | the printed `(option:…)` after a **restart** | `h01-links`, `h02-restored`, `h03-options` | the degradation, on purpose — see below |
| **i** | nothing; ten shots each of two binaries | `i-*-00`, `i-*-01` | the screen is unchanged — see below |

## The restart, and why it degrades rather than breaking (phase h)

Saved terminal text keeps SGR and drops every other escape (`Pane::sanitizeSgrOnly`: "any escape
sequence left in the file is stripped… when the backend produced ANSI-formatted scrollback we
keep the SGR sequences and strip everything else"). So a restored transcript has no OSC 8 at all
— no prose anchors, and no label runs — and the label there is the plain coloured text it was
before this card. **That is why the `(target)` is still printed after every label**: it is text,
it survives, and it is what opens after a restart. `h03-options` is the Options page reached that
way. Dropping the printed target for Relay's own schemes, which was the other option on the table,
would have made a restored `option:`/`session:`/`card:` link unreachable instead of merely
unhighlighted.

(The restart has to drop both `--fresh` **and** `--workspace`: `main()` reads either as "start
fresh" — `startFresh = parser.isSet(fresh) || parser.isSet(workspace)` — so a drive that passes
`--workspace` on the relaunch is measuring an empty pane, which the first pass of this drive did.)

## The pixel diff (phase i)

The control is **not** an older tip. Main moves under this checkout all day and its chrome moves
with it: measured against the tip before the first commit, 4,000–5,500 pixels differed per shot,
all of it in the tab strip and the status bar, none of it in the transcript — other sessions'
work, not this card's. So the control is **this commit's tree with the card switched off**:
`Pane::proseUriFor()`'s three call sites handing the renderer an empty anchor, so `MarkdownAnsi`
emits the bytes it emitted before the card and nothing else in the program differs at all.

Nine shots of each, 1500×1100, `compare -metric AE`, measured over the whole window and over the
transcript band alone (below the tab strip, above the status bar — the tab title carries a live
`cpu00%-mem00%` readout that differs between any two runs of anything):

| transcript | shots identical in the transcript | worst shot | worst whole window |
|---|---|---|---|
| **no links at all** (`count slowly to twenty`) | 9 of 9 | 82 px | 223 px |
| **five links in one paragraph** | 9 of 9 | **0 px** | **0 px** |

The link-bearing transcript is **byte-for-byte the same picture** with the card on and off, which
is the point: an OSC 8 run takes no cells. The 0–82 px in the plain transcript is the blinking
caret in its one cell (the gate's floor is 400). What actually changed on screen is not in a
still: hovering a label underlines it, the pointer becomes a hand, the tooltip shows the target,
and a click opens it.

## What is not covered here

- **GhosttyCore.** Only libvterm builds on this machine, so every phase above ran on libvterm.
  Nothing in this change is core-specific — the label is an ordinary OSC 8 URI, read through
  `VtCore::hyperlinkAt` / `hyperlinkRuns`, which both cores already implement, and the one new
  rule (merging the pieces of a block's anchor run by URI) is in `TerminalView`, above the core.
  The engine's own tests run on both cores where the machine has both; here they ran on libvterm.
- **The keyboard walk (Ctrl+Shift+L) was deliberately left alone.** The `(target)` is still
  printed, so every label's destination is already one stop on the walk; adding the label would
  give every markdown link two stops for one destination.
