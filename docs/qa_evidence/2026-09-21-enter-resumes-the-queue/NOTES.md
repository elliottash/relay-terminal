# #7JD1 — Stop pauses the queue, Enter resumes it

Owner, 2026-09-21: *"why don't we just copy the functionality and have enter resume."*

**The code is landed; the live drive is not finished.** This directory holds what was measured
before the session stopped, and the harness a next session should finish with. What is here is
honest about which of the three kinds of evidence exist.

## What landed

| sha | what |
|---|---|
| `2d2993e1` | the worker: `TurnSupervisor.submit` clears the pause on its way past |
| `c96bd853` | the card claimed, and the wire choice |
| `4df330d5` | the thread entry saying why the worker holds the rule |
| `69260659` | the pane and a card's console: Enter on an empty box resumes; the strip's hint |
| `b8582fea` | the device's card view: `board_resume {id}`, the eleventh request of §17.1 |
| `7d36ec3a` | the device's pane view: `queue_resume {pane}` |

**The wire choice** (the card left it open): the **worker** resumes on the submit, rather than the
pane sending `resume_queue` after its `ask`. That is the side that leaves a terminal pane's wire
byte-for-byte unchanged *whatever* the queue is doing — the pane sends nothing new in either case,
not only when nothing is paused — and it is the same code that makes a device's `ask`/`board_ask`
resume, which step 2 asks for anyway.

## The ten-shot terminal-pane pixel gate — done

`src/Pane.h` was touched, so the gate of `../2026-09-21-card-turns-final/term-drive.sh` was run
once on a clean export of the tip before the first commit (`d6682f97`, `term-before/`) and once on
a clean export of the landed tip (`7d36ec3a`, `term-after/`). Both runs: 6 PASS, 0 FAIL, and their
`notes.txt` are identical. `term-diffs.txt` has the counts; every one of them is read, not waved
away:

| shot | pixels | what it is |
|---|---|---|
| 02, 03, 03b, 05, 06 | **0** | identical |
| 04-toolrow | 142 | the tab icon's busy-spinner animation frame |
| 07-queued | 259 | the spinner frame, the turn clock (8 s → 9 s) and the caret |
| 08-esc, 09-after | 2131 | **this card's own change**: the paused strip's hint, `hint-before.png` → `hint-after.png` — "↑ take back to edit · drag to reorder · × remove" becomes "Enter resumes · ↑ take back to edit · × remove", beside the same Resume button |
| 01-fresh | 21310 | **not this card's**: the subagents status line ("Automatic agent turns: 0 used of 50 …") is on screen in one run and not the other. `src/SubagentsPanel.cpp`, which writes it, is byte-identical between the two tips (`git diff d6682f97 7d36ec3a -- src/SubagentsPanel.cpp` is empty), so it is a timing difference between two runs and cannot be a change in behaviour |

The two tips are 19 commits apart because this checkout is shared and `main` moved while the work
landed; the `src/` diff between them is `git diff --stat d6682f97 7d36ec3a -- src/` — this card's
five files plus `BoardPane.cpp` and `RelayWindow.h` from #R6BS, neither of which draws a terminal
pane.

## The live drive — NOT finished

`drive.sh` (three phases: `empty`, `typed`, `card`) and `stub-provider.py` beside it are written
and were run once. **The first run failed four checks and the failures were the harness's, not
Relay's**: the stub logged only the first 400 characters of each user message, and Relay prefixes
a turn's prompt with its context block — so the words that were typed, which are at the *end* of
that message, were never seen. The stub then never picked its slow scene, every turn answered in a
fifth of a second, and there was no running turn to queue a prompt behind. That is fixed in
`stub-provider.py` here (it logs the tail, marks the pane's helper turns so a prompt is not counted
twice, and picks the scene off the untruncated text) and **has not been re-run**.

So a next session's remaining work is:

1. `drive.sh <relay> <out-dir>` on a build of the landed tip — all three phases. The checks are
   already written; what they are worth is that `requests.jsonl` shows *when* the queued prompt
   reached the model, which is the whole claim of the card.
2. The device: the remote protocol's own tests cover it
   (`tests/test_board_protocol.py::AskTests::test_board_resume_runs_a_stopped_cards_queue_again`
   and `test_a_devices_next_ask_resumes_the_card_it_stopped`), but a scripted device session —
   `board_cancel` then `board_ask` — has not been driven live.
3. Then `#7JD1` to `needs-verification` with this path in `links.evidence`.

## The tests that are run and passing

- `tests/test_queue.py` (37) — `test_a_submit_runs_now_and_resumes_the_paused_queue` replaces
  `test_now_runs_while_queue_paused`, whose last two lines asserted the behaviour this card
  changes; `test_a_queued_submit_resumes_too_and_relays_own_does_not`.
- `ctest -R continueturn` (8) — an empty box over a paused queue resumes, text in it does not, and
  a pause does not change what Ctrl+Enter means (#SXF1 kept).
- `ctest -R consolemode` (19) — a card console's Enter sends `resume_queue` with
  `surface: "card:K7Q2"`, Enter with text sends the ask alone and no resume, an unpaused box sends
  nothing at all.
- `ctest -R boardremote` — the device allow-list is eleven, `board_resume` rebuilds into the
  worker's `card`, and `board_resumed` is an answer and never a broadcast.
- `tests/test_board_protocol.py` (174), `tests/test_remote_board.py` (60),
  `tests/test_remote_pane_state.py` (38), `tests/test_remote_security.py` (69),
  `tests/test_requests.py` + `tests/test_board_turns.py` (62).
