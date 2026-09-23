---
id: 0VEG
type: work
status: done
labels: [bug, signal]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: signal collection:unittest:tests.test_board_protocol, 2026-09-23
links: {plans: [], commits: [36ca8f6], evidence: [], related: [], github: null, signal: collection:unittest:tests.test_board_protocol}
---
# collection:unittest:tests.test_board_protocol fails

## Issue
`collection:unittest:tests.test_board_protocol` has failed 4 time(s) in 4 run(s) since 2026-09-23T18:43:15Z.

```
scope execution
```

## Signal
<!-- Written by Relay (relay_core.signals.signal_section). Rewritten in place whenever
     the signal changes; edit around it, not inside it. -->
- **key** `collection:unittest:tests.test_board_protocol` · broken · **resolved**
- failing executions: 4 in 4 run(s); first seen 2026-09-23T18:43:15Z, last 2026-09-23T18:45:06Z
- resolved at 2026-09-23T19:03:05Z in `419f057dcab1`
- it resolves on 2 consecutive passing executions of that key, and on nothing else: closing this card does not close the signal, and this card cannot leave needs-verification while the signal is open.

```
scope execution
```

## Done means
The Board protocol test module imports in the Board test runner and its Plan-turn assertions match the current Run wording and Plan contract. The collection signal stops reproducing on two consecutive runs.

## Tests
`tests/test_board_protocol.py::AgentWiringTests::test_plan_mode_keeps_the_reads_and_drops_the_writes`
`tests/test_board_protocol.py::CardScopeAgentTests::test_a_plan_turn_is_refused_the_writers_when_it_calls_them_and_told_about_execute`
Full module: `PYTHONPATH=backend:tests python3 -m unittest tests.test_board_protocol -q` (180 passed).

### Check 2026-09-23 15:04
- passed · unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes — tests/test_board_protocol.py::AgentWiringTests::test_plan_mode_keeps_the_reads_and_drops_the_writes passed for this revision on spark-dcc9, 2026-09-23T19:04:28Z
- passed · unittest:tests.test_board_protocol.CardScopeAgentTests.test_a_plan_turn_is_refused_the_writers_when_it_calls_them_and_told_about_execute — tests/test_board_protocol.py::CardScopeAgentTests::test_a_plan_turn_is_refused_the_writers_when_it_calls_them_and_told_about_execute passed for this revision on spark-dcc9, 2026-09-23T19:04:28Z
- notice · unittest:tests.test_board_protocol.CardScopeAgentTests.test_a_plan_turn_is_refused_the_writers_when_it_calls_them_and_told_about_execute — tests/test_board_protocol.py::CardScopeAgentTests::test_a_plan_turn_is_refused_the_writers_when_it_calls_them_and_told_about_execute is slow: p95 0.00 s, p50 0.00 s
history: thread
