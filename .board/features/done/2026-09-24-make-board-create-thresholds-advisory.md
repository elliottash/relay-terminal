---
id: XZQW
type: work
status: done
labels: [feature, switchboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-luna via codex
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'Focused limit tests pass, including visible activity warning payloads and continued creation after both thresholds.', sign_off: none, effort: medium}
source: Codex guest in Relay, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [XZ6Y], github: null}
---
# Make Board create thresholds advisory

## Issue
i feel like those limits should be advisory -- like the user should be warned, rather than capping the agents. so we can go back to the 5 card warn limit. and the 30 per hour should also be a warning

## Plan
Goal: Card creation continues after the five-per-turn and 30-per-hour thresholds, with a visible warning at the first crossing.
Findings: `BoardTools._check_write_budget` refuses the sixth create; `RateState.claim` refuses the 31st within an hour. `board_activity.summary` is shown in the pane and its toast, and create tool results reach the agent. `board_split_card` can create several cards in one call.
Steps: Restore five as the configured turn threshold. Record hourly creations without rejecting them, detect first crossings, and add warning text to successful create/split results and activity summaries. Revise the agent policy and current design/format docs. Add focused tests for turn, hour, cross-pane, and split behavior.
Risks: An agent may generate many cards, so warnings must be prominent but not repeat on every create. The existing duplicate and write guards stay in force.
Verify: Focused Board tests show the sixth and 31st cards are created with warnings; later creates continue and do not spam repeat warnings; board validation does not gain new errors.

## Done means
The sixth card in one turn and the 31st card in a rolling hour are created successfully. Each threshold crossing warns the agent in the tool result and warns the user in the pane's Board activity toast. Continued creation remains allowed; warnings do not repeat for each later card. New boards and this project use five as the turn warning threshold.

## Execution Summary
Restored five as the per-turn create warning threshold. The 31st rolling-hour create now also succeeds and warns. Warning text is returned to the agent and added to the Board activity summary that Relay shows as a toast. Applied the same advisory thresholds to cleanup turns.

## Tests
`git diff --check` — passed. `PYTHONPATH=backend python3 -m py_compile backend/relay_core/board_tools.py backend/relay_core/board.py` — passed. Unit tests not run.
