---
id: BP15
type: work
status: discussing
labels: [bug, landing, tests]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: 'found while landing #V3R3, 2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [3MH4, V3R3, HEY7], github: null}
---
# The landing gate fails on main itself, so no code job has landed since the queue cutover

## Issue
Every code job since the 2026-09-26 queue cutover (#3MH4) has failed the accepted gate: 17 of 17 between 05:13 and 07:44 UTC, with 13–15 ctest failures out of 122 plus pytest failures in scripts/test.sh. Only Board metadata jobs land. Jobs for #V3R3 (7603a50692f95acd) and #HEY7 (d6c0df4f8d6ef262) fail the identical 26 tests, so these are baseline failures on `main`, not caused by any one submission. The 26: ctest modelspane, settings, keymap, conversations, queuesubmit, consolemode, backends, panestatus, boardworkspace, cardtests, boardfilter, projectinit, calllines, backend-and-bash (timeout). pytest: test_prompt_profiles (a board adds five tools), test_project_probe (board folder reported), test_relay_free_e2e (exhausted allowance), test_openrouter_catalog (catalog rows), test_provider_limits (coding plan limits), test_questions (two esc-skips tests), test_relay_pro (real transport), test_remote_board (statuses), test_presets (GUI mirror), test_pane_view OutboxTests setUpClass, and the C5 model-box test. Gate logs: ~/.local/state/relay/integration/12c8c9ef12cf3b12/logs/<job>/verify-*.log. A second fault: the handoff reads 'conflict in: (no paths found)' and 'Sync your workspace to the current target, resolve these files', which describes a merge conflict. The actual cause is a gate failure. The reconciler then refuses because the submission exceeds 24 files, so resyncing and resubmitting cannot help.
