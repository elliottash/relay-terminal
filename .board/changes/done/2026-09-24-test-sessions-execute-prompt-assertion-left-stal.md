---
id: JKG2
type: work
status: done
labels: [bug, tests]
implemented_by: kimi/kimi-k3
verified_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
source: '#GPF7 run, 2026-09-24'
links: {plans: [], commits: [f7df0d36], evidence: [], related: [GPF7, BGRN], github: null}
---
# test_sessions execute-prompt assertion left stale by #BGRN's "Run the plan" rewording

## Issue
Measured while running #GPF7's plan (2026-09-24): `PYTHONPATH=backend:tests python3 -m unittest test_sessions.PlanModeTests.test_the_execute_prompt_follows_the_plans_orchestration_block` fails at tests/test_sessions.py:751 — asserts prompt.startswith('Execute the plan in /tmp/plan.md:') but planning.execution_prompt returns 'Run the plan in /tmp/plan.md:'. Commit 854c097d (#BGRN) changed the wording in backend/relay_core/planning.py without updating the test. Reproduces with HEAD's agent.py, so it is independent of #GPF7's fix.

## Resolution
Asserted the committed wording `Run the plan in` (one line, tests/test_sessions.py:751, commit f7df0d36). `PYTHONPATH=backend:tests python3 -m unittest test_sessions -q`: 54 tests, OK. Small fix, verified in the same turn.
