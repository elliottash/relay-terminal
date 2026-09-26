---
id: 346F
type: work
status: inbox
labels: [bug, tests, plugins]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
source: 'Found by pane b65a84fc while implementing #WYGY, 2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [WYGY, 83YV], github: null}
---
# test_workspace_plugins: 3 Python-kernel tests fail on main (route language 'ipython' vs 'python')

## Issue
On a clean `git archive` export of main e5850f48, run with `PYTHONPATH=backend`, `tests/test_workspace_plugins.py` has 3 failures:
- `RoutingTests::test_python_workspace_routes_code_questions_and_forced_prefixes`: the route reports `language: 'ipython'` where the test expects `'python'`.
- `KernelTests::test_deactivate_and_shutdown_close_the_kernel`.
- `WorkerTests::test_worker_activates_routes_and_runs_a_kernel_line`.

#WYGY found these while running its targeted tests; its change does not touch them (the results are identical with and without it). Either the tests or the kernel runtime's language name needs to follow whichever change introduced 'ipython'.
