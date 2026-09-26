---
id: BP15
type: work
status: executing
assignee: codex
labels: [bug, landing, tests]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: 'found while landing #V3R3, 2026-09-26'
links: {plans: [], commits: [5a48a94f, 0bd768f9, 3372d487], evidence: [], related: [3MH4, V3R3, HEY7], github: null}
---
# The landing gate fails on main itself, so no code job has landed since the queue cutover

## Issue
Every code job since the 2026-09-26 queue cutover (#3MH4) has failed the accepted gate: 17 of 17 between 05:13 and 07:44 UTC, with 13–15 ctest failures out of 122 plus pytest failures in scripts/test.sh. Only Board metadata jobs land. Jobs for #V3R3 (7603a50692f95acd) and #HEY7 (d6c0df4f8d6ef262) fail the identical 26 tests, so these are baseline failures on `main`, not caused by any one submission. The 26: ctest modelspane, settings, keymap, conversations, queuesubmit, consolemode, backends, panestatus, boardworkspace, cardtests, boardfilter, projectinit, calllines, backend-and-bash (timeout). pytest: test_prompt_profiles (a board adds five tools), test_project_probe (board folder reported), test_relay_free_e2e (exhausted allowance), test_openrouter_catalog (catalog rows), test_provider_limits (coding plan limits), test_questions (two esc-skips tests), test_relay_pro (real transport), test_remote_board (statuses), test_presets (GUI mirror), test_pane_view OutboxTests setUpClass, and the C5 model-box test. Gate logs: ~/.local/state/relay/integration/12c8c9ef12cf3b12/logs/<job>/verify-*.log. A second fault: the handoff reads 'conflict in: (no paths found)' and 'Sync your workspace to the current target, resolve these files', which describes a merge conflict. The actual cause is a gate failure. The reconciler then refuses because the submission exceeds 24 files, so resyncing and resubmitting cannot help.

## Done means
The accepted code gate passes on an immutable commit derived from baseline main, with its tests asserting intended behavior under an isolated environment. A code submission for #BP15 publishes and has a receipt.

## Plan
1. Reproduce and classify baseline gate failures from the queue logs and local targeted runs.
2. Repair test isolation, assertions, or gate policy according to the failure cause; avoid metadata reconciliation modules.
3. Run targeted checks and accepted gate, submit the fix, and record the publication receipt.

## Tests
`#736Y` accepted gate log `~/.local/state/relay/integration/12c8c9ef12cf3b12/logs/cc97c4eb6e4351df/verify-1-5c0aa1919162.log`: CTest reported 14/122 failures. `build/Testing/Temporary/LastTest.log` records stale C++ expectations in modelspane, settings, conversations, keymap, projectinit and other tests. `backend-and-bash` times out at 600 seconds. The accepted gate then runs `scripts/test.sh` separately, but `set -e` stops before that step on CTest failure.

## Execution Summary
In workspace `wt1db244c507e8f826`, commits `5a48a94f`, `0bd768f9`, and `3372d487` repair the queue-submit starting/steering race; align moved-source and renamed-UI tests; extend and isolate the Python gate; and isolate guest session fixtures. Targeted CTest: 12/12 passed (`backends|boardworkspace|calllines|conversations|keymap|modelspane|settings|projectinit|panestatus|queuesubmit|boardfilter|cardtests`). Targeted Python modules: action catalog 9/9, approvals 68/68, board import 50/50, board protocol 184/184, guest accounts 26/26, CPace 15/15, guest session rows 17/17; additional named regressions passed. The full Python run was stopped after 76 known failures because it inherited real guest homes and was scanning them; the runner now isolates those homes. The accepted gate has not yet passed or published.
