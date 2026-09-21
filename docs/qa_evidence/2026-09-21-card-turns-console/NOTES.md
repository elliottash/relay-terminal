# Card #CTRN, steps 4 and 5: the card console draws the turn (GUI)

The GUI half of *"a card turn is an ordinary console turn"*, driven under Xvfb against a **copy**
of a two-card fixture board. Private `HOME` / `XDG_*` / `TMPDIR` under a short path, `RELAY_KEYRING=off`,
and no provider account at all: the profile points a local model endpoint at `stub-provider.py` on
loopback and pins the **helper role** (13.1) to it, so every agent in the run is that script.
Nothing here calls a provider.

```
bash drive.sh /path/to/relay "$PWD"              # the whole run below
bash drive.sh /path/to/relay "$PWD" card queue   # one phase, or several
```

Commits: `21660474` (step 4), `d9577277` (step 5), `dc083b31` (what this drive found: the page was
still refusing a second prompt, and the card's console had no size).

**What the checks are gated on.** The widget rectangles `RELAY_QA_RECTS` writes — a widget that is
not in the dump is not on screen — the bytes of `issues/threads/<ID>.md`, the conversation files
under the helper store, and `relay.log`'s per-pane event lines. Not on reading the terminal. The
one thing read by OCR is the card page's **thread view**, cropped to its own rectangle first,
because "nothing of the turn is drawn here" is the claim under test and the same words are
supposed to be in the console below it.

`notes.txt` is the run's own log, one PASS/FAIL line per check naming the shot it was read from:
**24 passed, 5 failed**, and every failure is one of the three below.

## What the run shows

1. **The turn is drawn in the card's console** — `c02a-fold.png`. Under the busy strip, in the
   console's own transcript: `▸ stub`, `▸ ✦ thought for 1 s` (the pane's thinking fold, and the
   hint that teaches Alt+R), `▸ read fixture.txt · 1 line` (the tool row) and the answer as it
   streams. This is the Issue #AGNT was filed for, arriving on the last surface without it.
2. **The thread view draws none of it** — `c02-threadview.txt` is the thread view alone, read off
   its own rectangle while the turn ran: the owner's entry and the stage event, and no `thinking`,
   no `thought for`, no tool line, none of the answer. `c03-threadview.txt`, after the turn, has
   the settled entry. Until this card the same bytes were drawn twice.
3. **The strip is the label and the ✕** — `boardBusyLabel` is `✦ Switchboarding · discussing…` and
   `boardStop` is `✕ Stop discussing` (#VZ69's wording, owner decision 4), and **`boardBusyWhat`
   is not on screen at all**: the progress line the tool rows replace is gone, widget and all.
4. **The thread is the record, written by the worker** — `thread-<ID>.md`: the owner's entry at
   submit, the stage event, and the answer with `author=agent kind=comment mode=discuss model=stub
   turn=<session>/<turn>` (owner decision 2), attributes in that order, no heading line.
5. **A card's events reach one console** — `c06-panes-*.txt`. `relay.log` writes one line per event
   per pane (`Pane::logEvent`), so the distinct `pane=` ids on the turn's `tool_started`,
   `agent_started` and `agent_finished` *are* the routing check: **one** pane, with the board's
   console open in the same tab. Before step 5 every console of the tab was handed all three.
6. **Two cards at once, and Stop names one** — `d03-both-running.png`, `d04-stopped.png`. A second
   card starts its own turn while the first is working (#DR4K, #0Z13), and ✕ ends the turn on the
   card it is on.
7. **A second Enter queues instead of bouncing** — `q01-queued.png`, `thread-<ID>-queue.md`. Both
   questions are on the thread and both turns answered, in order, with two different `turn=` ids.
   No `board_busy`: before `dc083b31` the page dropped the second line on the floor.
8. **A Plan turn is offered the writers and refused at call time** — `p01-refused.png`: the
   `CardScope.refusal` sentence, which names Execute (owner decision 3), and the turn carries on
   and lands its answer with `mode=plan` (`thread-<ID>-plan.md`).

## What this drive found, and what it could not prove

* **The card page was still refusing the second prompt** (fixed in `dc083b31`). `CardDetail::submit`
  dropped an agent line while `m_busy`, `plan()` returned early and the row greyed Plan out — the
  guards that matched the worker's old `board_busy` refusal. The worker queues now, so the guards
  were the only thing left stopping it.
* **The card's console had no size** (fixed in the same commit). `updateConsoleHeight` sized the
  list page's console only; the card's stayed at its size hint, which was right while the turn was
  drawn in the thread view above it. It draws the turn now, and the drive found it two lines tall
  with the fold and the tool rows scrolled out of it.

Three checks fail, and none of them is about what the card page draws:

* **The queued prompt has no row on screen while it waits** (`q1`, three lines). The §12 strip is
  drawn from the pane's **own client-side queue** (`Pane::m_entries`, filled when a line typed into
  its composer is held back because the agent is busy), and a card's prompt never goes that way:
  `CardContext::submit` sends it as `board_ask`, so it waits in the **worker's** queue, which
  arrives as `queue_changed` and which `rebuildQueueStrip` does not read. The prompt does queue and
  does run (`q3`); what is missing is the row. Drawing it needs `src/Pane.h` — either the strip
  reading `queue_changed`'s rows, or a host call that hands the pane a queued row — and no step of
  #CTRN may open that file (the plan's Risks 8). It is on the card's thread for the owner.
* **"and the other card is still working"** (`d3`, the last line). The stub's doing, not Relay's: a
  card turn's prompt is seeded with the card's own file **and its thread**, so by that point card
  A's thread already holds the first phase's `trace the card please` and the stub answers *that*
  scene — in a second — rather than the long one those words ask for. The half of the check that is
  Relay's (✕ names one card) is the line above it; the other half (the other card runs on and ends
  `done`) is in the backend drive's evidence, `docs/qa_evidence/2026-09-21-card-turns-backend`.
* **The restart** (`r3`). `r06-tab-before.txt` and `r07-tab-after.txt`: the restarted window came
  back with a **different tab id**, and a card's conversation is keyed per *(tab, card)* — so a
  different conversation is the correct answer to a different tab, and this run cannot say whether
  the key is right. What it does show is that the card's turn is in a helper conversation file of
  its own (`r1`, `r03-card-session.txt`) and that the thread is complete after the restart (`r2`).
  The per-(tab, card) file is proved directly in the backend drive (its item 6).

## Two things worth an owner's eye

* **One console per card page, not per card.** The card page builds one console and reuses it for
  whatever card is open, so switching cards leaves the previous card's turn in the transcript under
  the new card's title (seen in an earlier run of this drive). The conversation and the routing are
  still per card — the context's `surface` and persist key follow the open card — but the scrollback
  is shared. Clearing it on a card change needs a call on `relay::agent::ConsoleHandle` and a
  `src/Pane.h` change, so it is a decision rather than a fix to slip in here.
* **A segfault on quit.** One run's `relay` died with `Segmentation fault (core dumped)` when the
  drive sent it SIGTERM at the end of a phase. It did not recur in the final run, nothing was lost
  (the thread and the conversation files are written by the worker), and the tree also held other
  sessions' uncommitted work, so this is a note rather than a finding.

## Files

| file | what |
|---|---|
| `drive.sh` | the driver: fixture board, the five phases, the checks |
| `stub-provider.py` | the loopback endpoint; the `trace the card` scene is reasoning, then a tool call, then prose |
| `notes.txt` | the run's own PASS/FAIL log |
| `c02a-fold.png`, `c02-running.png`, `c03-settled.png` | the turn in the console, the thread view empty, then settled |
| `c02-threadview.txt`, `c03-threadview.txt` | the thread view alone, cropped to its rectangle and read |
| `c06-panes-*.txt` | the distinct panes handed the turn's events: one |
| `d0*.png` | two cards working at once, and ✕ Stop on one |
| `q01-queued.png`, `q02-both-done.png` | a second Enter while the card works |
| `p01-refused.png`, `p02-plan-done.png` | the Plan turn's refused write and its answer |
| `r0*.png`, `r06/r07-tab-*.txt`, `r03-card-session.txt` | the restart, and the tab id that did not survive it |
| `thread-*.md` | the card's thread after each phase, raw |
| `relay.log` | the run's diagnostics log (debug level, for the per-pane event lines) |

## Tests

`ctest --test-dir build -R '^board$|^boardpane$|^boardworkspace$|^consolemode$'` — green.
`boardexecute` fails 3 of 5 exactly as #48S3's thread records (its `findChildren<QPushButton *>
("boardExecute")` predates #AGNT's action row, and its CMake registration is still an uncommitted
hunk in the shared checkout); unchanged by these commits.
