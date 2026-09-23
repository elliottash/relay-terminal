# Refine (#6W9X): evidence

Implementation commit: `d77495b` on main. `scripts/land.py` built the exact landed tree
(`--target relay`) before the swap.

| File | What it shows |
|---|---|
| `test_board_tools-refine.txt` | 15 scope tests, the 7 new Refine ones and the 8 Plan/Discuss ones beside them, all pass |
| `test_board_protocol-refine.txt` | the two new `ModeTests`: wordless Refine is recorded as "Refine this card.", the prompt carries `[Refine · #ID]` and the brief, the card stays in the inbox; the whole file passes (182) |
| `ctest-board.txt` | `relay-board-tests`: the Refine button, the `refine` `board_ask`, the "refining…" strip, and the row order Plan, Refine, Run |
| `boardremote-refine.txt` | a phone's `board_ask {mode: refine}` goes through and is said as "Refine on #K7Q2 from iPhone" |
| `refine_transcript.py` / `.txt` | Refine's write rules against a temp copy of this repo's real `issues/`, on #3BPH: related links, a known label, a missing Done means and its own note land; a partial `links`, an invented label, the issue, the plan, the title, a move and another card are refused, each with a sentence naming where that belongs; the status is unchanged |

Not captured here: a live Refine turn in the app with a model. The tools, the prompt and the GUI
wiring are each tested; whether the brief produces a useful note on a real near-duplicate card is
the judgement for the verifier's Try it.

Unrelated failures seen while testing, present before this commit:
`boardremote` `executeGoesThroughTheWindowsHookAndClaimsTheCard` expects "Execute on" where #BGRN
changed the status to "Run on"; `boardexecute` is #3BPH; `boardworkspace` fails two source-text
checks about `deliverToConsoles` and `Pane::openOutputTarget`, neither touched here.
