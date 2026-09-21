# The AI's pass over the #7BM4 scenarios (2026-09-20)

Played by the implementing session (Claude Fable 5.1) with `ai-pass.sh`, in a real Relay under
Xvfb on an isolated profile, against a project staged by `stage.py`. Binary: the build gate's,
from `f72960e2`. Input was clicks, chords and text typed into the board's own filter box; nothing
was typed into a terminal pane. Each step of `scenario.json` whose actor is `both` was played; the
steps marked `human`, and each scenario's question, were left for the person.

| Step | Expected | Seen | Shot |
|---|---|---|---|
| 1.1 open the "done" card | strip: 4 listed, never checked; thread says all tests pass | as expected | `ai-pass/a-10-card.png` |
| 1.2 Check | gone, failing, never-run; a dated block in the body; nothing run | as expected — `rounding` gone, `totals_large_order` failed last run, the three invoice cases never ran; the `### Check` block is in the body | `a-11-checked.png` |
| 1.3 move to Done | refused, tests named, Override…, picker back | as expected; the notice was cut off mid-id (finding 1) | `a-12-status-open.png`, `a-13-gate.png` |
| 2.1 Tests | summary 7 tests · 3 passed (1 slow, 1 flaky) · 1 failed · 3 never run; `inventory_sync` first, flaky | as expected, 70 %, red and green cells | `b-10-pane.png` |
| 2.2 click the row | reliability, flake score, last failure, history by host | as expected, but only three lines of the detail fit (finding 2) | `b-11-detail.png` |
| 3.1 Profile → Build (this machine) | a table with `src/report.cpp.o` first | as expected: 8 steps, 2.6 s wall, `report.cpp.o` 2.5 s, 94.1 % | `c-10-menu.png`, `c-11-result.png` |

## Findings

1. **The gate's notice was unreadable** — it listed every Python case by its full dotted id and ran
   out of box. Fixed on `main` while this pass ran: a line that names many tests is one finding
   (`a646b1d1`, `a2204ba4`) and the notice uses short names (`b9cd344f`): "(rounding,
   test_invoice, totals_large_order)". The binary used here predates both, so the screenshots
   show the long form.
2. **Opened below a Switchboard narrower than 900 px, the Test suites and Profile panes get about
   a third of the height**: one table row and three lines of detail. Everything is there, but a
   person has to maximise the pane to read a failure. Left for the owner's judgement in the human
   pass — the split ratio is a product choice, and it sits in `src/RelayWindow.h`, which five
   sessions hold.
3. **The board shows "2 signals"** above Inbox on the staged project: #AQ6X's machine-written
   faults picked up the flaky and the failing test from the same history. Not part of #7BM4; it
   agrees with what the pane says.
4. **Driving by coordinates is brittle.** The same click missed on a board with fewer label rows.
   A QA agent (#YZ8G) needs to drive Relay by name — open card X, press the button called Check,
   read the notice text — not by pixel; `app_action_run` and the pane model are the seams to grow.
5. **A clean export of current `main` crashes at startup on a fresh profile** (#561P), found while
   preparing this pass. An AI pass is what caught it before a person was handed a dead binary.
