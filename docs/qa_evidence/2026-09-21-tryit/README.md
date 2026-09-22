# Try it, driven for real on #7BM4 (#JNYN, 2026-09-21)

The live drive of protocol § 31.10. Not a unit test and not a stub: a real Relay on an isolated
Xvfb display and an isolated profile, the Switchboard agent on its configured model
(`kimi|kimi-k3`, the role in the owner's own `relay.conf`), pressing **Try it** on this
repository's own card **#7BM4** — a card in Needs verification, about the app.

Everything the turn itself produced is under `docs/qa_evidence/2026-09-21-tryit-7BM4/`, which is
the directory Try it made for it. This folder holds the drive: the screenshots, and a copy of the
`## Try it` section the turn wrote, so the section can be read without opening the card.

## How it was driven

```
Xvfb :160, HOME/XDG_RUNTIME_DIR/TMPDIR/XDG_{CONFIG,DATA,CACHE}_HOME under a short sandbox,
relay.conf seeded with onboarded=true, approvals_chosen=true and [roles] switchboard\preset=kimi,
switchboard\model=kimi-k3. The keyring is deliberately NOT disabled — the Switchboard agent has
to reach a real model — and no key is printed, logged or in this folder.
./build/relay --workspace /home/elliott/repos/relay-terminal --clean-shell
Ctrl+Shift+S → filter `7BM4` → the row → the card page → Try it (y).
Input is xdotool only, and never into a terminal pane.
```

## A. What the screenshots of the real turn show

| file | what |
|---|---|
| `a1-try-it-button.png` | the card page for #7BM4 in Needs verification, with **Try it (y)** on the action row beside Verify (disabled here: no verifier is available for this card) |
| `a2-button-row.png` | the row itself: Plan (p) · Execute (x) · Verify (v) · Try it (y) |
| `a3-running-notice.png` | one press: the notice line reads `Try it · #7BM4 · 0:05 · Model is reasoning…`, and the button has become **Trying… (y)** and is disabled, so a second press cannot start a second staging |
| `a4-running-tasks.png` | the same run at 2:45. The task list the turn made from the brief, in the brief's own order: read the card, stage the fixture under `/tmp/claude-…/tryit` with `stage.sh` and `staging-notes.md`, the mechanical pass under Xvfb with one screenshot per step, the sealed `expected.md`, then `## Try it` |

## B. The section, the button and the answer, end to end

The turn's own mechanical pass takes as long as a mechanical pass takes, so the *second* half of
the feature — the `## Try it` section rendered, its **Open it** button, and `try_answer` — was
driven separately and deterministically, on a disposable copy of the same `orders` fixture
(`stage.py --dir /tmp/claude-…/jn-try`), with a `## Try it` written onto its seeded card #KB7Q and
the expected result sealed in a file beside it. Same binary, its own Xvfb display and profile,
xdotool only.

| file | what |
|---|---|
| `b1-section-strip.png` | the strip: `Try it · Did it stop you, and did it tell you why quickly enough that you would use it?` in amber, an **Open it** button, and the one-line answer box. The section's list marker is not in the question |
| `b2-section-on-the-card.png` | the whole card page with the strip between the `## Tests` strip and the body |
| `b3-open-it-ran-the-command.png` | **Open it** pressed: a terminal pane opened beside the board and *ran* the section's one line — `bash …/stage.sh`, its output, `Shell ready · exit 0`. Nothing was typed into an existing pane |
| `b4-answer-box.png` | the answer typed into the box (one line, Enter sends) |
| `b5-answer-recorded.png` | the notice — "Answer recorded on #KB7Q. The expected result is under `## Try it`, and `## Human QA` is written from it." — and the activity chip `#KB7Q · replaced ## Human QA` |
| `b6-strip-after-the-answer.png` | the strip afterwards: *answered — the expected result is under the section* |
| `b-card-after.md` | the card's `## Try it` and `## Human QA` after the answer: the seal broken under the section, and Human QA generated as a numbered question with an indented `Answer:` line, which is the shape `board_tools.unanswered_human_qa` reads (it answers `[]` for this body, so the close gate is satisfied) |

The thread gained, in this order: a `decision` entry quoting the person
(`Try it · verdict on #KB7Q` / `Q:` / `A: "…"`), then the two write events for the append and the
replace. Nothing was typed twice.

**Found and fixed from looking at these**: the strip drew the question with the section's list
marker still on it (`Try it · 3. Did it stop you…`), and the revealed Expected lost its indent
under the numbered item in `## Human QA`. Both are fixed; `b1` and `b-card-after.md` are after
the fix.

## C. What the real turn had done when this was landed

The turn on #7BM4 was still in its mechanical pass when these commits landed — it is a real
staging of a real app and it takes as long as it takes. What it had produced by then, all of it
in `docs/qa_evidence/2026-09-21-tryit-7BM4/` (the directory Try it made for it, before the brief
was even sent):

- `stage.sh` — its own rerunnable staging script. It found the worked example's `stage.py` and
  reused it rather than writing a second fixture generator, which is what the brief's step 2 asks
  for, and it prints the one line that opens the fixture.
- the fixture itself at `/tmp/claude-…/tryit/orders`: a git repository, a board, and the seeded
  card #0FJ6 *Order totals are wrong above 100 items* sitting in Needs verification with the
  three problems the card was written about already happening.
- `drive.py` and `dbg.py` — its own xdotool driver for the mechanical pass, and the debug script
  it wrote when two of its clicks landed in the wrong place. It looked at its own captures and
  re-drove, which is the brief's "do not hand a person a pass you did not read".
- one capture per step: `01-board`, `02-filtered`, `03-card`, `04-checked`, `05-status-picker`,
  `06-gate`, `06b-back`, `07-suites`, `08-detail`, plus the `a-*` and `d-*` runs.
  **`06-gate.png` is the one to look at**: the staged card's move to Done refused, with the
  notice naming the two checks that do not prove it.

No `## Try it` had been written when this landed, and that is the honest state: the section is
written at the end of the turn, after the pass, which is exactly the order the brief fixes so
that a card never asks for a review of something that was not staged.

## D. How the real turn ended, and the one thing it found

It did not finish. After **30 minutes, 69 tool calls and 53 steps**, `kimi-k3` stalled at
`api.moonshot.ai` — sixty seconds with nothing produced, once, retried, and again — and the turn
ended `outcome=error` (`turn_end … ms=1801432 thinking_ms=902024 tools=69 retries=1
open_items=6`).

The feature did the right thing with that: **no `## Try it` was written on #7BM4**, the button
went back to **Try it (y)**, and the card was never put in front of anyone as ready to review.
That is the rule this whole card exists for, exercised against a real provider failure rather
than a stub.

But looking at the card afterwards showed a gap, and it is fixed rather than listed: **the card
carried no trace that Try it had been attempted at all.** The brief asks the *agent* for the
"Try it could not be staged: …" note (step 7), and an agent whose provider stalled — or whose
turn was stopped, or whose budget ran out — never reaches it. `TryItCommands._write_failure_note`
now leaves that note itself whenever a run ends `error`, `stopped` or `cancelled` with neither a
section nor a note of its own: same first words, plus how it ended, how long it ran, where the
captures are, and that the card is not asking to be reviewed. A card that was tried and failed
has to read differently from a card nobody pressed the button on. Four cases in
`tests/test_tryit_protocol.py` cover it, including "do not write over the agent's own note".
