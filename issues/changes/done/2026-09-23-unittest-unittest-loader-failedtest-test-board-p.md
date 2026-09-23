---
id: XJ2B
type: work
status: dropped
labels: [bug, signal]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: signal unittest:unittest.loader._FailedTest.test_board_protocol, 2026-09-23
links: {plans: [], commits: [36ca8f6d], evidence: [], related: [0VEG], github: null, signal: unittest:unittest.loader._FailedTest.test_board_protocol}
---
# unittest:unittest.loader._FailedTest.test_board_protocol fails

## Issue
`unittest:unittest.loader._FailedTest.test_board_protocol` has failed 4 time(s) in 4 run(s) since 2026-09-21T23:34:23Z.

```
ImportError: Failed to import test module: test_board_protocol
Traceback (most recent call last):
  File "/usr/lib/python3.12/unittest/loader.py", line 137, in loadTestsFromName
    module = __import__(module_name)
             ^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/elliott/repos/relay-terminal/tests/test_board_protocol.py", line 20, in <module>
    import fake_cards
ModuleNotFoundError: No module named 'fake_cards'
```

## Signal
<!-- Written by Relay (relay_core.signals.signal_section). Rewritten in place whenever
     the signal changes; edit around it, not inside it. -->
- **key** `unittest:unittest.loader._FailedTest.test_board_protocol` · broken · **resolved** · regressed
- failing executions: 6 in 6 run(s); first seen 2026-09-21T23:34:23Z, last 2026-09-23T18:45:06Z
- resolved at 2026-09-23T19:03:05Z in `419f057dcab1`
- it resolves on 2 consecutive passing executions of that key, and on nothing else: closing this card does not close the signal, and this card cannot leave needs-verification while the signal is open.

```
scope execution
```


## Resolution
The failed import came from the Board runner importing `tests.test_board_protocol` with only `backend` on `PYTHONPATH`. `tests/test_board_protocol.py` now adds its sibling fixture directory to `sys.path` (commit `36ca8f6d`), and this signal resolved on two consecutive recorded passes at 2026-09-23 19:03:05 UTC. #0VEG owns the same module collection repair; no separate fix remains.
