---
id: PJ2H
type: work
status: done
labels: [bug, signal]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: signal unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes, 2026-09-23
links: {plans: [], commits: [36ca8f6], evidence: [], related: [], github: null, signal: unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes}
---
# unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes fails

## Issue
`unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes` has failed 3 time(s) in 3 run(s) since 2026-09-23T18:44:50Z.

```
Traceback (most recent call last):
  File "/home/elliott/repos/relay-terminal/tests/test_board_protocol.py", line 1030, in test_plan_mode_keeps_the_reads_and_drops_the_writes
    with self.assertRaises(ValueError):
AssertionError: ValueError not raised
```

## Signal
<!-- Written by Relay (relay_core.signals.signal_section). Rewritten in place whenever
     the signal changes; edit around it, not inside it. -->
- **key** `unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes` · broken · **resolved**
- failing executions: 3 in 3 run(s); first seen 2026-09-23T18:44:50Z, last 2026-09-23T18:45:06Z
- fingerprint: `AssertionError: ValueError not raised`
- resolved at 2026-09-23T19:02:38Z in `419f057dcab1`
- it resolves on 2 consecutive passing executions of that key, and on nothing else: closing this card does not close the signal, and this card cannot leave needs-verification while the signal is open.

```
Traceback (most recent call last):
  File "/home/elliott/repos/relay-terminal/tests/test_board_protocol.py", line 1030, in test_plan_mode_keeps_the_reads_and_drops_the_writes
    with self.assertRaises(ValueError):
AssertionError: ValueError not raised
```

## Done means
The Agent wiring test reflects the current Plan mode contract: the tool list stays stable and Plan is an instruction rather than a permission boundary. It fails only if the policy or implementation changes again without the test matching it.

## Tests
`tests/test_board_protocol.py::AgentWiringTests::test_plan_mode_keeps_the_reads_and_drops_the_writes`

### Check 2026-09-23 15:04
- passed · unittest:tests.test_board_protocol.AgentWiringTests.test_plan_mode_keeps_the_reads_and_drops_the_writes — tests/test_board_protocol.py::AgentWiringTests::test_plan_mode_keeps_the_reads_and_drops_the_writes passed for this revision on spark-dcc9, 2026-09-23T19:04:28Z
history: thread
