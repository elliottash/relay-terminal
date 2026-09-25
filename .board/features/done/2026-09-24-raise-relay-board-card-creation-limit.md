---
id: XZ6Y
type: work
status: done
labels: [feature, switchboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'Focused Board limit tests pass, and a fresh board accepts 20 creates but refuses the 21st.', sign_off: none, effort: low}
source: Codex guest in Relay, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Raise Relay Board card creation limit

## Issue
i want to raise the relay limit, unless its well justified. why do we ahve that cap at all?

## Plan
Goal: Allow a legitimate batch of more than five Board cards in one turn.
Findings: `board_tools.py` defaults to five; `board.py` writes five into new `board.yaml`; this workspace also overrides the value to five. The separate hourly ceiling is 30 and the cleanup workflow already uses 60 per turn.
Steps: Set the normal per-turn limit to 20 in the runtime default, generated board config, and this workspace config. Align the format and design docs. Keep the hourly and write limits. Run focused Board tests and board validation.
Risks: A runaway agent can create more cards in one turn, bounded by 30 per hour per workspace and existing duplicate checks. No user decision needed.
Verify: Assert a fresh board uses 20, this workspace config reads as 20, and a 21st create is still refused; run focused tests and `relay-board.py check`.

## Done means
A normal Relay Board agent can create up to 20 cards in one turn in a new board and in this repository. A 21st create returns `board_rate_limited`; the 30-per-hour workspace guard still applies. Documentation describes the active limits.

## Execution Summary
Raised normal per-turn Board creates from 5 to 20 in the runtime default, generated board config, and this repository's board.yaml. Updated format and design documentation. Kept the 30 creates per hour workspace ceiling and duplicate checks.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_board_tools.LimitTests` — 6 passed.
Fresh generated board boundary probe — 20 creates accepted; 21st returned `board_rate_limited` with turn scope. Defaults, scaffold, and this board config all read as 20.
`python3 scripts/relay-board.py --board .board check` — blocked by one unrelated existing error: a different card's `verify` block lacks `effort` (plus existing warnings).
