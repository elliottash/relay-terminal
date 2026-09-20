# #PPR4 — per-turn GUI cost stops growing, and the Activity pane stops re-rendering the block

Two leftovers of the #PF4K profile
(`docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md`): finding 3, "cost per turn grows
with the length of the conversation", which the engine implementer's fold-walk work (`b8e91fe3`) did
not move; and the Activity pane's `setThinking`, which that profile could read in the code but could
not measure because it could not open the pane.

Machine: spark (aarch64, 20 cores), Xvfb on a private display, a fresh `HOME`/`XDG_*`/`TMPDIR`,
`RELAY_KEYRING=off`, `--clean-shell`, window 1400×900, the stub provider from the profiler's harness
(`docs/qa_evidence/2026-09-20-perf-profile/transcript/stub.py`), no provider key. Both sides are
clean exports built RelWithDebInfo with frame pointers; "after" is the same export plus this card's
hunks and nothing else. Load average 1.8–3.1 throughout (five other #PF4K agents were on the box).

Scripts here: `rounds.sh` (the alternating before/after rounds), `drive.sh` (the profiler's, plus `PERF2_*` for a second perf window and `POST_KEYS`
for the Activity pane's shortcut), `growth.sh` (N turns back to back), `think.sh` (one long
reasoning block with the pane open), `turncost.py` (the engine implementer's reducer).

## A. What grows per turn: the request ledger, sent whole, three times a turn

The profiler's finding 3 looked at the fold walks. They were not it. The channel was:

    IPC=1 ./drive.sh ipc 6 'zturn 1' … 'zturn 120'     # the shim tees both directions of worker.py

`ipc.txt`: over 120 back-to-back turns, **`requests` is 95.8 % of every byte the worker sends
the GUI** — 8.66 MB of 9.05 MB — and it grows with the conversation, from 2 KB at turn 1 to 143 KB
at turn 120 (three events a turn, ~400 bytes an entry, the newest 200 entries every time). Nothing
else on the channel grows at all: everything but `requests` is 1 236 bytes a turn at turn 10 and
1 268 at turn 120.

A ceiling measurement confirmed it before anything was written: the same binary with the worker
patched to list no entries at all (`turns-probe-norequests.txt`) kept 60 % of the growth out.

Two things were wrong, and both are fixed:

- **The whole ledger on the wire every time.** `requests_delta` (protocol 12.11): the worker sends
  the entries that *changed*, with `removed` beside them. Default off, so an older GUI and a phone
  behind one see exactly what they saw. A ledger that is not the one already sent — a new chat, a
  load, a resume, a rewind, or the reply to the `requests` command — still goes whole, and the
  worker makes that decision so the GUI never applies the "another ledger?" test to a partial list.
- **O(entries²) in the GUI.** `RequestLedgerModel::handle` looked up each incoming entry's previous
  self with a `std::find_if` over the whole list: 40 000 QString comparisons per event at the 200-
  entry cap, three times a turn. It is one hash lookup per entry now, on both paths.

### 250 turns, GUI-thread CPU per turn (`./growth.sh <binary> <tag> 250`)

| turns | before r1 | after r1 | before r2 | after r2 | before mean | after mean |
| --- | --- | --- | --- | --- | --- | --- |
| 1 – 50 | 47.1 | 52.2 | 38.2 | 46.1 | **42.7** | **49.2** |
| 51 – 100 | 42.0 | 40.4 | 41.2 | 48.8 | **41.6** | **44.6** |
| 101 – 150 | 47.3 | 36.1 | 57.1 | 60.6 | **52.2** | **48.4** |
| 151 – 200 | 63.9 | 56.7 | 68.8 | 62.7 | **66.4** | **59.7** |
| 201 – 250 | 71.0 | 56.5 | 63.5 | 68.6 | **67.3** | **62.6** |
| **turn 225 / turn 25** | 1.51× | 1.08× | 1.66× | 1.49× | **1.58×** | **1.29×** |

ms of GUI-thread CPU per turn (`turns.txt`). **Read this one with the caveat.** Five other #PF4K
agents were profiling on the same box: the load average sat between 3.8 and 5.1 and a single window
moves by ±15 % between rounds, which is the same size as the effect. The rounds alternate for that
reason, and what they agree on is the *shape* — the ratio between the 225th turn and the 25th falls
from about 1.6× to about 1.3× — not a number to quote. **The honest measurements of this fix are
the two deterministic ones**, which do not care what else is running:

- **the wire** (`ipc.txt`): worker→GUI bytes over 120 turns **9 046 034 → 594 129 (−93.4 %)**, and
  the `requests` event goes from 2 049 bytes at turn 1 and 119 459 at turn 100 to a **flat ~1 765
  bytes at every turn**. Nothing on the channel grows with the conversation any more;
- **the work** (`tests/requests_test.cpp::aLedgerEventDerivesTheTaskListOnce`): over a ledger that
  grows to two hundred entries, with the chip, its state, its tooltip, the turn-end line and the
  task list all read on every change, the task walk runs exactly **once per event** — 200 events,
  200 derivations. It used to be six to eight, each building a TaskItem of five QStrings per listed
  request.

A quieter earlier pair is at the foot of `turns.txt`: the clean export 32.7 → 50.2 ms/turn, and the
ceiling probe — the same binary with the worker patched to list no ledger entries at all —
29.2 → 38.0, which is what said the `requests` event was about 60 % of the growth before a line was
written. **The remainder is not in this card's code**: with the ledger off the curve still rises
9 ms over 200 turns, from the scrollback and the fold layer the conversation accumulates. The
orchestrator's re-measurement on a quiet sphinxpad is the one to trust for the flatness target.

## B. The Activity pane re-rendered the whole reasoning block at 4 Hz

`AgentInternalsView::setThinking` rendered the *whole* block through `foldForMarkdown` and wrote all
of it into the `QTextDocument` again on every flush — four times a second while a block streams.

Driving it: the pane's own shortcut, `Alt+Shift+R` (`agent.internalsPane`, `src/Keymap.h`), pressed
two seconds after the prompt goes out, which is the `Pane::attachInternals` path — a pane opened on a
block already streaming. `think.sh` does it and screenshots the result
(`activity-pane-open.png` here) so the pane is provably open. The stub streams
`zthink200000r200`: 200 000 characters of reasoning in 20-character deltas at 200/s, so the block
grows at about 4 000 characters a second and the run walks the whole size range.

**200 KB, not 400 KB.** `kThinkingChars` in the view is 400 000, but the pane's buffer that feeds it
is capped at 200 000 characters per turn (`src/Pane.h`, the `thinking_delta` branch), so 400 KB
cannot be reached from a worker. The three sizes below are 50 KB, 100 KB and 200 KB.

| reasoning block | before | after |
| --- | --- | --- |
| 50 KB (12 s in) | 34–42 % of a core | **7.1 %** |
| 100 KB (25 s in) | 58 % | **7.8 %** |
| 200 KB (50 s in) | **84 %** | **7.2 %** |
| whole block, GUI-thread CPU | **26.77 s** | **3.96 s (−85.2 %)** |
| RSS over the block | +5.9 MB | +4.6 MB |

GUI-thread CPU per second as the block grows (`think-before.txt`, `think-after.txt`). Before, the
cost of a flush is the size of the block, so the curve is a ramp from 5.7 % to 84.4 % of a core;
after, it is the size of the *delta*, so it is flat at 6–10 % from the first second to the last.
This one needs no caveat: it is a 12× difference on a box that moves by 15 %.

The fix renders the block a chunk at a time: `relay::calllines::MarkdownStream` (`src/CallLines.h`)
keeps the streaming Markdown renderer between flushes and hands back the rows that have *settled* —
they are appended once and never touched again — plus a short tail (the line still being written, a
table still open, the trailing blanks a finished render trims) which is the only part redrawn.
`tests/calllines_test.cpp::aStreamedBlockRendersExactlyAsAWholeOne` checks the result against
`foldForMarkdown()` span for span, at every split of a corpus with fences, lists, tables, links and
an unclosed fence, and then a character at a time.

Two more things the pane does not do any more:

- **Nothing at all while it is hidden** (another tab, a splitter dragged shut). The block is held
  and drawn when the pane is shown — or the moment anything else has to print under it, which is
  what keeps the log in the order things happened
  (`tests/agentinternals_test.cpp::aBlockHeldWhileHiddenKeepsItsPlace`).
- **A full re-render only on a theme change.** A fold row's colours are in its spans, so what is
  written keeps the theme it was written in — every row in this log does — but the block still
  streaming used to be re-rendered whole on each flush and so followed a theme switch. It still
  does, once, from `refreshTheme()`. Width needs none: the view wraps the document itself.

## Tests

- `tests/calllines_test.cpp` — `aStreamedBlockRendersExactlyAsAWholeOne` (streamed == whole, span
  for span, at every split), `aStreamedBlockSettlesEachRowOnce` (2 000 rows fed in 100-character
  chunks settle once each and the redrawn tail never exceeds two rows).
- `tests/agentinternals_test.cpp` — `reasoningStreamedInDeltasReadsAsOneRender` (character-at-a-time
  deltas leave the same document as one call), `aBlockHeldWhileHiddenKeepsItsPlace`.
- `tests/test_request_stream.py` — the filter: first event whole, then only what changed; an entry
  that falls off the end is `removed`; another ledger goes whole; the reply to the `requests`
  command goes whole; and a receiver that merges 30 ledgers ends up holding exactly the full list
  each time.
- `tests/requests_test.cpp::aDeltaReplacesWhatItNamesAndKeepsTheRest` — the GUI half of that.
