---
id: N7RJ
type: work
status: needs-verification
labels: [bug, phone, remote, tests]
assignee: agent
implemented_by: glm/glm-5.3
session: fe70deb7-5c8a-4470-a5de-ad3ef11db698
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: the suite passes with the real app.js loading through the fixture (PYTHONPATH=backend python3 -m unittest tests.test_board_view), sign_off: none, effort: low}
links: {commits: [0fc83778], evidence: [], github: null, plans: [], related: [ESDF]}
---
# Phone test fixture lost pairedHint: whole test_board_view suite red since 14d7f422

## Issue
Noticed while re-verifying #ESDF at tip: the whole phone browser suite fails (29/34 in tests/test_board_view.py) with `SyntaxError: The requested module './rrp.js' does not provide an export named 'pairedHint'`. 14d7f422 (#Q5QJ #SAW4, 2026-09-24) added `pairedHint` to app/rrp.js and imported it in app/app.js, but tests/fixtures/board/fake_rrp.js — which the suite's server substitutes for /app/rrp.js — still re-exports only the pre-#SAW4 names, so the real app.js dies at module load and every browser test times out. The product app is fine; only the fixture is stale.

## Done means
- `PYTHONPATH=backend python3 -m unittest tests.test_board_view` passes all its tests at tip, with the page under test the real `app/app.js`. Failure: any test dying at module load with `does not provide an export named 'pairedHint'`.

## Tests
- Before (at tip 960c0e70, fixture unfixed): `PYTHONPATH=backend python3 -m unittest tests.test_board_view` → `Ran 34 tests in 883.408s, FAILED (failures=29)`, every failure the module-load `SyntaxError: ... 'pairedHint'` timeout.
- After the fix (0fc83778): same command → `Ran 34 tests in 24.246s, OK` (34/34). Includes #ESDF's five phone board tests.
- Desktop at the same tip, for the card that surfaced this: `ctest --test-dir build -R '^(board|boardfilter|boardpane)$'` → 3/3 passed.
