---
id: GPF7
type: work
status: inbox
labels: [bug, tests, planning]
assignee: null
rank: mgpf7
created: '2026-09-21'
source: 'Codex regression run for #XP7N'
links: {plans: [], commits: [], evidence: [], related: [XP7N], github: null}
---
# Guest planning test assumes PLAN MODE is the first prompt text

## Issue
Measured during #XP7N: test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back fails at line 382, asserting sent.startswith("PLAN MODE."). The failure reproduces on a clean git archive of a63f33f1542f, before the exit-plan changes. Guest instruction injection now precedes the planning prompt.

## Tests
`PYTHONPATH=backend:tests python3 -m unittest test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back -q`
