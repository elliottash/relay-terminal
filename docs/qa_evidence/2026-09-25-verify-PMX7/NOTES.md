# Verify PMX7 — plan mode phantom model changes (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). Commit e654c416 is an ancestor.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_roles tests.test_plan_turns` — **OK** (the migration/regression suites the card names; grew from its 110).
- `xvfb-run -a ./build/relay-jobstab-tests -silent` — **23 passed, 1 failed**; the failure is the #E8V1 rename fallout (tests/jobstab_test.cpp:181, #SYTR), not the plan-route path. The card's 1/1 pass predates that rename.
- `scripts/relay-build --target relay` — passed earlier this sweep.

## Done means
- One-time retirement of the pre-#HR5E `roles/planning/*` override, kept explicit Jobs overrides, same settings for terminal and console agents, shell commands never enter the plan route, and regressions against legacy-override survival / repeat migration / stale UI copy — **passed by test evidence**: the migration and plan-turn cases in tests.test_roles + tests.test_plan_turns are green at HEAD; no live re-drive of an upgraded installation was done (needs a seeded legacy config; the suites construct it).
- Implementer's README evidence — **present** (docs/qa_evidence/2026-09-21-phantom-plan-model-PMX7/).

## Verdict
PASS.
