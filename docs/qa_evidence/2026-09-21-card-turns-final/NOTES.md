# Card #CTRN, the final pass: the queue strip, the transcript and the restart

What the steps 4–5 drive left open, closed and driven under Xvfb. Three phases against a **copy**
of a four-card fixture board, a private `HOME` / `XDG_*` / `TMPDIR` under a short path,
`RELAY_KEYRING=off`, and **no provider account**: the profile points a local model endpoint at
`stub-provider.py` on loopback and pins the Main tier list and the helper role to it, so every
agent in the run is that script. Nothing here calls a provider.

```
bash drive.sh /path/to/relay "$PWD"              # the three phases below
bash drive.sh /path/to/relay "$PWD" queue        # one phase, or several
bash term-drive.sh /path/to/relay <dir>          # the ten-shot terminal-pane gate, separately
```

Commits: `7a5d7f27` (the strip, the transcript hand-over, the quit crash), `acda4552` (the two
things this drive found) and `5f834ce7` (a card's row taken back the way #QRC1 takes back every
other one). The binary driven is a clean `git archive` export of `5f834ce7`, built
outside the shared checkout — the shared tree holds six other sessions' uncommitted work in
`src/Pane.h` alone.

`notes.txt` is the run's own log, one PASS/FAIL line per check naming the shot it was read from:
**18 passed, 0 failed**.

## 1. A queued card prompt has a row in the §12 queue strip

`q03-queued.png`, and this is the owner's original complaint on this card — *"the queue doesn't
work like the main terminal"* — on the last surface it was still true of.

The card page, with a turn running and a second Enter sent: under the console's transcript is the
strip, `QUEUE`, `▸ running  ✦ count slowly for me`, and a row reading **`✦ trace the card
please ✕`**. The row's text is the owner's own words, not the prompt the model is sent — a card's
prompt is the card's seed block and the mode's brief around them, and the row would otherwise
read "[Switchboard card #… ] You are Relay's Switchboard agent…". The running line has its words
too, which took `acda4552`: a console's own prompt is kept out of `m_itemPrompts` on purpose, so
`runningLabel()` had nothing to read and drew `▸ running  ✦` with the words missing. The hint
line is the strip's own, in #QRC1's wording: `↑ take back to edit · drag to reorder · ✕ remove`.

The row is read off `queueList`'s own rectangle, cropped before it is read, so "the strip has a
row in it" is a claim about the strip.

**And the affordances reach the card's queue, not the tab's.** ↑ on the empty prompt box takes
the row back — #QRC1 landed `3ebf3673` in the middle of this pass, and that session had already
read this card's rows into `recallQueueHead` — so the op is a `queue_remove` naming **this
card's** queue, the row leaves the strip (`q04-taken-back.png`, `q04-queuerows.txt`) and the
**whole** prompt comes back into the box as an unsent draft (`q04-draft.txt`), not the
120-character preview the row was drawn from. The draft is thrown away here, and the prompt that
was withdrawn never ran: one agent answer on the thread, the first turn's. Its *question* is
still there, unanswered, which is owner decision 5 — a withdrawn queued prompt leaves what a
failed turn already leaves.

## 2. One console, several cards

`s01-cardC.png` → `s02-cardD.png` → `s03-cardD-answered.png` → `s04-cardC-again.png`, each read
on the console's own region (the window under the thread view's rectangle).

Card C's turn is drawn in the card console. Opening card D draws **none** of it — the same
widget, a different card's transcript — and card D's own turn is drawn in it. Opening card C
again brings its transcript back: the tool row `▸ read fixture.txt · 1 line`, the answer, the
`✦ 1 tool call` line, and the rule the replay prints under it. Card D's turn does not come back
with it.

The conversation and the routing were already per card (the context's `surface` and its persist
key); this is the pixels, and it is what one console per open card would otherwise have cost —
a second emulator, a second attachment to the tab's worker and a second queue strip for every
card anyone opens, which is the plan's Risk 4.

## 3. The restart, decided

The steps 4–5 drive could not decide this one: its restarted window came back with a **different
tab id**, and a card's conversation is keyed per *(tab, card)*, so a different conversation was
the right answer to a different tab.

**The drive was wrong, not the code.** `src/main.cpp` reads an explicit `--workspace` as "start
fresh here" exactly as `--fresh` does — `startFresh = parser.isSet(fresh) || parser.isSet(workspace)`
— so `launch --keep`, which dropped `--fresh` but kept `--workspace`, skipped
`restoreSavedLayout()` and opened a brand-new window with a brand-new tab. This drive's restart
passes neither option; the subshell is already in the workspace and the option defaults to the
current directory.

With the layout actually restored: **the tab id is the same on both sides** (`r03-tab-before.txt`
and `r06-tab-after.txt`, `t97d445f927ee`), the card's thread is complete, and the turn after the
restart went into **the same conversation file** as the turn before it
(`…/helper-sessions/f2a61dfee00c1f48/b3e4f38028c7630417875bac845f4904.json`, byte-for-byte the
same path). The model says so itself: `HISTORY turns=2` — the stub answers with the number of
user messages the worker handed it, so two is the earlier turn plus this question.

#FEJQ's `tab_id` is saved by `serializeTab` and read back by `addTab`; nothing was wrong with it.

## 4. The terminal pane is unchanged

`terminal-diffs.txt`, `term-before/` and `term-after/`. The ten-scene drive is #AGNT step 1's
own, copied here as `term-drive.sh` with one change — the profile pins the **Main tier list** to
the stub, because since #MDL1 a pane starts on rank 1 of that list and a fresh profile takes the
worker's defaults, which on this machine is the Claude Code harness. Run without the pin the
"terminal pane" was running a guest, answered "Not logged in", and not one pixel of it was about
Relay.

Both runs pass the same six OCR checks. Six of the ten shots are pixel-identical, and every
non-zero number is something the drive cannot hold still between two runs of the *same* binary:
a startup toast still up in one run's first shot, the blinking caret, the busy spinner's
animation frame, and one more word of the stub's stream before Esc landed. The §12 queue strip
in `07-queued` is identical below the header. `terminal-diffs.txt` has the numbers, the boxes
and a picture of each.

**`term-after/` is a build of `7a5d7f27`, not of the tip**, and that is deliberate: it is the one
commit of this pass that touches anything a terminal pane draws, so the pair isolates it. The
other two cannot reach a terminal pane at all — `acda4552`'s `runningLabel` fallback is only read
when `m_itemPrompts` has no entry for the running item, which a pane's own `ask` always leaves,
its new pair of replay rules is only printed by `clearTranscript`, which no terminal pane calls,
and `5f834ce7` is inside `removeWorkerRow`, which a pane with no worker rows never reaches.

A comparison against **today's tip** is in `terminal-diffs-tip.txt` (`term-tip/`, a build of
`5f834ce7`), and it is large — 22 181 px below the header on `01-fresh` alone. None of it is
this card's: sixty-odd commits from other sessions landed in the same evening, and the visible
one is #SHP7 moving the share button beside the prompt box's folder chip
(`terminal-tip-share-button.png`, before above, after below). It is here so that the numbers the
owner asked for exist and nobody reads them as a regression. The tip passes the same six OCR
checks (`term-tip/notes.txt`).

## What this drive found

Two things, both fixed in `acda4552` and both re-driven here:

* **The "▸ running" line said nothing on a card.** A console's own prompt travels as its
  context's message and is deliberately kept out of `m_itemPrompts`, so `runningLabel()` had
  nothing to read and the strip drew `▸ running  ✦` with the words missing.
* **"this shell is new" on a surface with no shell.** The transcript a card page brings back was
  printed between the *conversation's* rules (#0TJ9), whose closing one says exactly that. A
  console has no shell and the text is not a conversation's — the card's conversation is the
  worker's file and outlives the pixels.

## Files

| file | what |
|---|---|
| `drive.sh` | the driver: the four-card fixture, the three phases, the checks |
| `term-drive.sh` | the ten-shot terminal-pane gate, copied from #AGNT step 1's drive |
| `stub-provider.py` | the loopback endpoint, copied from the steps 4-5 drive |
| `notes.txt` | the run's own PASS/FAIL log |
| `q0*.png`, `q03-queuerows.txt`, `q04-draft.txt` | the queued row, its text, and what ↑ takes back |
| `rect_composer.py` | the card page's prompt box out of the widget dump, for the check above |
| `s0*.png`, `s0*-console.txt` | the transcript changing hands between two cards and coming back |
| `r0*.png`, `r03/r06-tab-*.txt` | the restart: the same tab id, the same conversation file |
| `thread-*.md` | the cards' threads after the phases, raw |
| `term-before/`, `term-after/`, `terminal-diffs.txt` | the ten-shot pixel comparison, isolating |
| `term-tip/`, `terminal-diffs-tip.txt`, `terminal-tip-share-button.png` | the same drive on today's tip, and why its numbers are large |
| `terminal-diff-*.png` | a picture of each difference the comparison found |
| `relay.log` | the run's diagnostics log |

## One card is fenced per phase, and why

The phases share a board, and a card turn's prompt is seeded with the card's own file and the
tail of its thread — so a keyword an earlier phase typed on a card is in every later prompt on
it, and the stub answers the *earliest* keyword in the message. The steps 4-5 drive recorded that
as a failure of its own ("the stub's doing, not Relay's"). One card per phase is the way round
it, and the restart phase's first prompt is deliberately a sentence with no scene keyword in it
at all, so the question after the restart cannot be hijacked by the answer before it.
