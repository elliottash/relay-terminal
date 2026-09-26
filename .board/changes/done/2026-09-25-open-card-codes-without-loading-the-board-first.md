---
id: E0JY
type: work
status: done
labels: [bug, board, performance]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
verified_by: openai/gpt-6-sol via codex:ashe-ethz-ch
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-25
links: {plans: [], commits: [8ea6fee15002, 9e2b0bcb24ca, feeb9251c4a9, a58514e1b3e9], evidence: [], related: [], github: null}
---
# Open card codes without loading the board first

## Issue
A card-code click waits for the full Board listing and helper console before showing the requested card. Open the named card directly and draw its page before initializing the helper agent.

> performance issue -- laoding cards is slow. it sems like it loads the board. 
>
> clicking card codes should go straight to the card without laoding the board. 
>
> and the card should be loaded before loaidn the helper agent.
> — elliott · [session:8a72f719af574b349714271b21f7bd71](relay://session/8a72f719af574b349714271b21f7bd71) · 2026-09-25

## Done means
Clicking a #CODE opens that card without waiting for a board listing response.
The card document is displayed before its helper console is created.
Returning to the list still loads the full board, and missing card codes report a failure.

## Execution Summary
Commits `8ea6fee1`, `9e2b0bcb`, `feeb9251`, and `a58514e1` make card-code links, notification jumps, own-pane reveals, and restored solo cards request only the named card. The worker prepares the file-backed Board, returns the card, and configures the helper after the card page appears. Returning to the list loads the full Board. Card lookup uses the generated index with a front-matter fallback. The local `scripts/relay-build --fast --target relay` completed after the restore-path fix.

## Tests
### Check
Pass: isolated `relay-board-tests` build and `ctest -R '^board$'` on the selected tree.
Pass: `PYTHONPATH=backend python3 -m unittest tests.test_board.DirectCardReadTests tests.test_board_protocol.WriteTests.test_a_card_detail_read_round_trips_through_the_protocol` (3 tests).
Pass: `PYTHONPATH=backend python3 -m unittest tests.test_board_protocol.KeylessWorkerTests.test_direct_card_read_precedes_helper_configuration` (card response precedes the helper `configured` event).
Pass: landing gate built the exact tree for both commits.
