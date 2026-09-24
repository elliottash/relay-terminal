---
id: P4XN
type: work
status: inbox
labels: [bug, tests, protocol]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: pane bd9e4ae0 delivering #Q8TM, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# plan_execute prompt no longer starts with "Execute the plan" — test fails on plain HEAD

## Issue

`tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute`
fails on a clean export of main, before any local change. Measured 2026-09-24, main at
`79dd56a`:

```
git archive HEAD | tar -x -C /tmp/pristine
cd /tmp/pristine && PYTHONPATH=backend python3 -m unittest \
  tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute
→ FAIL: self.assertIn('Execute the plan in ', sent[-1]['content'][:24])
 AssertionError: '# Plan Restore Plan' does not contain 'Execute the plan in '
```

The prompt the worker sends after `compact → resume → recap → plan_execute` now begins with
`# Plan Restore Plan` instead of `Execute the plan in …`. Either the plan-execute opening
regressed on main (the agent is handed a plan title where it used to be handed an instruction),
or the opening was deliberately reworded and this test was not updated with it. Which side is
right needs the owner or the session that changed the plan opening; not fixed in passing while
delivering #Q8TM.
