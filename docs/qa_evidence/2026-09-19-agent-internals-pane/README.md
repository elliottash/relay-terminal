# #QT8C — the agent internals pane: implementer evidence

Implementer: Claude Fable 5.1 fork (Claude Code session), 2026-09-19, after an Opus 5 subagent
drafted `src/AgentInternalsView.*` and `src/InternalsLedger.*`. These are implementer shots, not
QA verdicts; the card's checklist is untouched.

`drive.sh [build-dir] [scene…]` runs Relay under Xvfb with an isolated HOME / XDG_* / TMPDIR,
`RELAY_KEYRING=off`, and `stub-provider.py` as the model: every ask is one streamed reasoning
block (~30 lines), a `run_command` that sleeps 4 s, two `read_file` calls the terminal merges
into "read 2 files", and a one-line answer. `implementer-notes.txt` is the OCR of every shot,
left half (the terminal) and right half (the pane).

## What the shots show

| Shot | What it shows |
|---|---|
| `implementer-open-empty.png` | Alt+Shift+R: the pane opens beside the terminal, empty, its header saying what closing it will do |
| `implementer-open-stream.png` | The reasoning streams into the pane under "✦ thinking…"; the terminal has only the request and "▸ stub" |
| `implementer-open-tools.png` | "✦ thought for 5 s", the run row, the merged "read 2 files" row — all in the pane; the terminal shows the answer and the "✦ 3 tool calls" line, no tool rows |
| `implementer-open-two-turns.png` | A second turn under its own rule in the pane; the terminal still has no rows |
| `implementer-closed.png` | **Closed: both turns reprinted** under "── request ──" rules, each row settled and collapsed, below the answers that printed meanwhile |
| `implementer-closed-unfolded.png` | A reprinted "ran sleep +1" row clicked: its fold opens in place (command, output, "open in pane") |
| `implementer-closed-twice.png` | Opened and closed again with nothing new: nothing printed twice |
| `implementer-midturn-closed.png`, `-done.png` | Closed while the turn ran: the rows so far reprinted, the live rows continued under them, then the answer |
| `implementer-midblock.png`, `-done.png` | Opened mid-reasoning: the inline fold settled to "▸ ✦ thinking moved to the internals pane", the pane has the block from its start under the turn's rule |
| `implementer-less-open.png`, `-quit.png` | Closed while `less` owned the screen: nothing printed into `less`; after `q`, the prompt came back and the reprint arrived |
| `implementer-twopanes-open.png`, `-asked.png` | Two terminal panes in one tab, each with its own internals pane; the ask goes to the second and only the second pane fills ("1 turn · 3 tool calls" against "0 turns · 0 tool calls") |
| `implementer-ownerclose-moved.png` | The pane moved to another split position (between the two terminals, away from its owner), still holding its owner's turn |
| `implementer-ownerclose-after.png` | …and the owner closed: the pane went with it, with nothing reprinted into a pane that is going |
| `implementer-hint.png` | Opened the slow way (the Actions palette): "Next time: Alt+Shift+R · agent internals" |

The blank line above each `── request ──` rule in `implementer-closed.png` is #5AWD's gap rule
(`beginBlock(Block::Header)` before the separator, so the gap lands above it and not between the
rule and the rows it introduces); inside a reprinted block the reasoning row and the tool rows are
set apart exactly as they are live.

## Test suites at landing

- `relay-internalsledger-tests`: 10 tests, `relay-agentinternals-tests`: 11 tests — all pass.
- `ctest --test-dir build`: 61 of 62 pass. The one red is `backend-and-bash`: 4 errors out of
  3,352 backend tests, three in `test_failover.SubagentFailoverTests` and one in
  `test_questions.PaneCardTests`. All four are Python in `backend/`, which this card does not
  touch, and `backend-and-bash` was already a known red in this checkout today.

## Not driven by the script (left for QA, and why)

- The hint's per-id show limit: each scene runs in a fresh profile, so the harness sees the first
  showing only. The limit itself is `relay::ShortcutHints`' own, shared with every other hint.
