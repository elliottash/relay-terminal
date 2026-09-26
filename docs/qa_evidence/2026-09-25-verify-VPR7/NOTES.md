# Verify VPR7 — new OpenRouter models require restart to appear in Priorities (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8. All three linked commits (3cb7ff33, 8d03da03, 78639301) are ancestors — the same landing as #CDP7/#MPA2.

## The card's own staged full-app check, re-run at HEAD
`RELAY_STAGE_PREFIX=verify-20260925 RELAY_STAGE_BINARY=$PWD/build/relay xvfb-run -a python3 docs/qa_evidence/2026-09-22-VPR7/stage.py`
- **Exit 0** — all asserts passed: 'late-model' absent before the trigger and present after (an open Priorities pane picks up late openrouter catalog events with no restart), the filter stays 'openrouter', the providers view shows '7 of 7', all six staged codex models list in Available/Enabled, and a helper `key_stored` event is recorded. Captures: `../2026-09-22-VPR7/verify-20260925-{before,after}.png`.
- The binary used is the shared-tree `build/relay` (its ModelsPane/ModelPicker/JobsTab/ModelCatalog sources match HEAD content; the tree's other in-flight edits are the BoardPane split, not models).

## Suites (clean worktree, this sweep)
- `ctest -R ^modelpicker$` 62/0 · `ctest -R ^modelcatalog$` 73/0 · `ctest -R ^modelspane$` 25/1 (the 1 = #E8V1 stale string, #SYTR; card recorded 21 passed pre-rename).
- `tests/test_openrouter_catalog.py` 1 failed, `tests/test_guest_harness_provider.py` 1 failed — both later drift (#Y4PJ defaults move, new harness), green at the card's own commit; filed #042V.

## Verdict
PASS.
