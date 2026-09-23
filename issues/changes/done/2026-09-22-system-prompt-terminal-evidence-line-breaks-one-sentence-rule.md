---
id: TXE1
type: work
status: done
labels: [bug, agent, tests]
assignee: agent
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: m
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22 (found while testing #1MGS)'
links: {commits: [da8d7ff8a94747444b122322dcbbf448fbf4fe50], evidence: [], plans: [], related: [PLDG], github: null}
---
# SYSTEM prompt's terminal-evidence line is five sentences on one line

## Issue
Found, not asked: `python3 -m unittest tests.test_system_prompt` fails at HEAD (`4ab14fc9`) with
`MovedRuleTests.test_system_keeps_one_sentence_per_line`: the line in `backend/relay_core/agent.py`
beginning "Relay may attach recent terminal command evidence to a turn." holds five sentences. The
owner's rule is one sentence per line in SYSTEM. `agent.py` last changed in `757d9d31` (#PLDG).

## Done means
`PYTHONPATH=backend python3 -m unittest tests.test_system_prompt` passes at HEAD, in particular `MovedRuleTests.test_system_keeps_one_sentence_per_line`; the terminal-evidence rule in `backend/relay_core/agent.py`'s SYSTEM is one sentence per line with no word changed. Failure shows as that test still naming the five-sentence line.

## Execution Summary
Split the five-sentence terminal-evidence line in `backend/relay_core/agent.py`'s SYSTEM (was line 375) into five lines, one sentence each, no word changed. Verified the failing test at HEAD first (`MovedRuleTests.test_system_keeps_one_sentence_per_line` failed with the line), then green after the edit: 18 tests OK. Landed in da8d7ff8; land.py held the hunk as contested because two live sessions (codex-model-ties, codex-xp7n-guest) hold agent.py snapshots — the hunk shown contained only this sentence split, none of their failover-chain edits, so it was confirmed.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_system_prompt` — 18 tests, OK (fails at HEAD before the fix on `MovedRuleTests.test_system_keeps_one_sentence_per_line`).
