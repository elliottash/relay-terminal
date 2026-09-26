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
links: {plans: [], commits: [8ea6fee15002], evidence: [], related: [], github: null}
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
Commit `8ea6fee1500270890197dd741f41cfc1daca6302` opens a clicked card code through `board_card_get` before requesting Board rows. The card document appears before its helper console is constructed. The full Board loads when the reader returns to the list. Card lookup uses the generated index with a front-matter fallback.

## Tests
### Check
Pass: `python3 scripts/land.py try cardfast --target relay-board-tests --tests '^board$'` built the exact selected tree and passed the Board test.
Pass: `PYTHONPATH=backend python3 -m unittest tests.test_board.DirectCardReadTests tests.test_board_protocol.WriteTests.test_a_card_detail_read_round_trips_through_the_protocol` (3 tests).
Pass: `python3 scripts/land.py commit cardfast ... --verify-target relay-board-tests` built the exact landed tree.
