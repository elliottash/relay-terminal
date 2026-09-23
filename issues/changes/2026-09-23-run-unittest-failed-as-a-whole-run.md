---
id: 229N
type: work
status: inbox
labels: [bug, signal]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: signal run:unittest, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [], github: null, signal: run:unittest}
---
# run:unittest failed as a whole run

## Issue
`run:unittest` has failed 4 time(s) in 4 run(s) since 2026-09-21T23:34:23Z.

```
11 of 11 executed tests failed in one run: one cause, not 11.
```

## Signal
<!-- Written by Relay (relay_core.signals.signal_section). Rewritten in place whenever
     the signal changes; edit around it, not inside it. -->
- **key** `run:unittest` · run · **resolved** · regressed
- failing executions: 6 in 6 run(s); first seen 2026-09-21T23:34:23Z, last 2026-09-23T18:45:06Z
- members: `unittest:unittest.loader._FailedTest.test_board_protocol`
- resolved at 2026-09-23T19:03:05Z in `419f057dcab1`
- it resolves on 2 consecutive passing executions of that key, and on nothing else: closing this card does not close the signal, and this card cannot leave needs-verification while the signal is open.

```
11 of 11 executed tests failed in one run: one cause, not 11.
```

