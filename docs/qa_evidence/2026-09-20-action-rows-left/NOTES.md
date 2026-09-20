# Action rows are left-aligned buttons and nothing else — #PBX1, 2026-09-20

Owner, comparing the card page's action row with the Switchboard panel's: *"the plan / execute
buttons etc, would those work better at the left? (in the switchboard card agent)"* — and then,
agreed: *"yes, lets do both left-aligned, drop the label"*.

This run drives a real Relay under Xvfb on the binary `land.py` built from the exact tree it
committed (`/tmp/claude-1000/land/actionrows/verify/build/relay`, commit `69fee7b2`), not this
checkout's `build/relay`, which carries other sessions' in-flight edits. Isolated HOME /
XDG_* / TMPDIR, `RELAY_KEYRING=off`, and no provider account: the one endpoint in the profile is
`stub-provider.py`, so every agent in the run is that script.

`./drive.sh [relay-binary] [out-dir]` — **22 checks, 22 passed** (`notes.txt` has one line each,
naming the screenshot it was read from).

## What each shot shows

| Shot | What it shows |
|---|---|
| `01-switchboard-idle.png` | The Switchboard beside a terminal pane. The panel's head row is `Check · Clean up · Tests · Profile`, starting at x=782 — the pane's own left margin is x=780, read off the INBOX section header. There is no "Switchboard agent" label in front of them; the box's placeholder ("Ask the Switchboard agent — Enter sends, a second prompt queues") is what names the agent now. |
| `02-turn-running.png` | The same panel mid-turn. The busy strip inside the prompt box reads `✦ Switchboard agent · 0:00  Requesting …` with `✕ Stop` at its right: the agent's name, **the turn clock** and (on a survey turn) the `· survey` word all moved here from the head row's label. The stub answers `slowly` so the strip lasts longer than a frame. |
| `03-switchboard-conversation.png` | After the turn. The head row is unchanged by a turn having run — still four buttons from x=782 — and the log has grown above the box. |
| `04-card-page.png` | A card page at 1500 px. `Plan (p)` at x=779 and `Execute (x)` at x=861, on the row above the reply frame; the card page's left margin (the body's "Issue" heading) is x=778. Nothing is on that row to the left of Plan. |
| `05-narrow-card-page.png` | The same card page in a ~350 px pane. `Plan (p)` at x=394 against a margin of x=393, `Execute (x)` at x=477: both whole, both with their keys, still left-aligned. |

`_approvals.png`, `_board.png` and `_list.png` are the run's own navigation shots (the first-launch
approvals pane, and the list each card was opened from), kept so the path through the app is on
the record.

## How the checks are made

"Left-aligned" is a claim about geometry, so a shot that merely *contains* the word "Plan" proves
nothing: every check here reads **x positions**.

- `leftmost` / `rowfirst` — nothing on the button's own line starts left of it. That is the whole
  rule in one measurement, and it is what a name label in front of the buttons would break.
- `near_left` / `rowmargin` — the first button starts on the pane's own left margin, read off
  something else that sits on it (the INBOX section header; the card body's "Issue" heading),
  because the Switchboard pane starts half way across the window.
- `order` / `roworder` — Check before Clean up, Plan before Execute: the reading order is the
  workflow.

The card page's row is read as a **band**: neither whole-page OCR pass reads `Execute (x)` — a
violet outline in violet type on the page's ground — so `buttonrow()` crops the row, triples the
saturation and flattens it to grey, which turns those strokes black on white. The band is placed
from the reply box's placeholder (68 px under the row), so it follows the layout rather than a
hard-coded y.

## What is not in this run

- The Options / Actions / Sessions panels. Their head rows have no actions, so the rule does not
  touch them: they keep the pane name and the fold control, and their own action row is the
  collapsed `? Helper Agent (Alt+Q)` row at the pane's bottom right (c3e8695c). The previous run,
  `../2026-09-20-prompt-boxes-like-the-pane/`, photographs all three.
- The `· survey` word. It is on the same label as the clock, written by the same call
  (`drawBusyLine`), and a survey turn only happens on a board Relay has never seen before; the
  unit test `BoardModelTests::theHelpersPromptBoxIsTheSameShapeAsAPanes` asserts the whole line,
  `✦ Switchboard agent · 0:42 · survey`.
