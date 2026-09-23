---
id: TXE1
type: work
status: inbox
labels: [bug, agent, tests]
assignee: null
rank: m
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22 (found while testing #1MGS)'
links: {plans: [], commits: [], evidence: [], related: [PLDG], github: null}
---
# SYSTEM prompt's terminal-evidence line is five sentences on one line

## Issue
Found, not asked: `python3 -m unittest tests.test_system_prompt` fails at HEAD (`4ab14fc9`) with
`MovedRuleTests.test_system_keeps_one_sentence_per_line`: the line in `backend/relay_core/agent.py`
beginning "Relay may attach recent terminal command evidence to a turn." holds five sentences. The
owner's rule is one sentence per line in SYSTEM. `agent.py` last changed in `757d9d31` (#PLDG).
