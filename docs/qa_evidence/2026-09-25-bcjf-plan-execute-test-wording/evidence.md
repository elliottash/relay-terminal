# #BCJF evidence: plan-execute test asserts #BGRN's "Run the plan in" opening

Fix: `9f9584ae` — `tests/test_session_protocol.py:371` now asserts
`startswith(f'Run the plan in {plan}:')` (was `'Execute the plan in {plan}'`).

Deciding evidence: `854c097d` (#BGRN) reworded `planning.execution_prompt` Execute→Run on
purpose; `f7df0d36` (#JKG2) already updated `tests/test_sessions.py` to match but missed this test.

## Before (clean export of main `a4584ec3`)

```
$ git archive HEAD backend tests | tar -x -C $D; cd $D
$ PYTHONPATH=backend python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute
  File ".../tests/test_session_protocol.py", line 371, in test_compact_resume_recap_and_plan_execute
    self.assertTrue(sent[-1]['content'].startswith(f'Execute the plan in {plan}'))
AssertionError: False is not true
FAILED (failures=1)
```

Actual prompt opening (printed from the same run):
`'Run the plan in /tmp/tmp72t7wgrc/ws/plan.md:\n\n# Plan\n1. edited by user\n\nWhere th'`

## After (clean export of landed `9f9584ae`)

```
$ PYTHONPATH=backend python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute tests.test_sessions -q
Ran 58 tests in 3.434s
OK
```

In the checkout: `PYTHONPATH=backend python3 -m unittest tests.test_sessions tests.test_session_protocol -q`
→ `Ran 96 tests in 31.408s OK`.

Sweep: `grep -rn 'Execute the plan' tests/ backend/ src/` → only the user-typed fixture string at
`tests/test_remote_control.py:1556` (not an assertion on the prompt).
