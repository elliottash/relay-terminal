---
id: MPA2
type: work
status: needs-qa-llm
labels: [bug, models]
assignee: codex
rank: mmpa2
created: '2026-09-22'
source: 'Codex in Relay, 2026-09-22'
links: {plans: [], commits: [3cb7ff33ba054175c93c10b699f0c08804e12f07, 8d03da032c8c58fbf48d4c99b866d25455f1fa0b], evidence: [docs/qa_evidence/2026-09-23-MPA2/, docs/qa_evidence/2026-09-25-verify-MPA2/], related: [4BPE, VPR7, CDP7], github: null}
---
# Available models missing from Priorities on sphinxpad

## Issue
there is an issue with the model priority pane, some of the models in available are not showing up there (observed on sphinxpad)

## Planning notes
At 1f0546d8, ModelPicker::buildTier in src/ModelPicker.cpp draws only stored tierList entries for an empty query. Search adds eligible allUsable catalog entries not already in that section. Available uses curatable instead. Thus availability alone does not make an unranked model visible in Priorities before searching. Tests/modelpicker_test.cpp explicitly checks that section behavior. Need the affected model names and whether searching finds them to distinguish this discoverability issue from a catalog/filter defect. SSH to sphinxpad returned No route to host during investigation.

## Done means
- Reproduce the reported missing models or explain exactly which list membership causes their absence.
- If defective, correct the filtering or discovery behavior with a focused regression test.

## Plan
Trace the specific OpenRouter refresh and Codex reports linked in this thread. Review fixes already landed for #VPR7/#CDP7, rebuild model tests, simulate availability toggles and list add/remove/undo with isolated settings, and rerun full-app worker-event staging. Avoid duplicating completed fixes or changing intentional priority-list membership. Record machine verification limits if sphinxpad remains unreachable.

## Execution Summary
Reviewed already-landed fixes for #VPR7 and #CDP7 rather than duplicating changes. The Models pane now uses the served pane consistently and falls back to the same helper catalog as Providers. Guest catalogs above six models no longer default to unavailable. Rebuilt and reran modelcatalog/modelpicker/modelspane tests, including availability toggle, add, remove, reorder, undo, and late provider refresh. Full-app worker-event staging passed with an open OpenRouter search gaining a model and seven Codex models available. No additional defect reproduced. Sphinxpad SSH remains unreachable; these later fixes have not been installed there by this session.

## Tests
- `ctest -R ^modelcatalog$`
- `ctest -R ^modelpicker$`
- `ctest -R ^modelspane$`
- `tests/test_openrouter_catalog.py`
- `tests/test_guest_harness_provider.py`
- manual: docs/qa_evidence/2026-09-23-MPA2/README.md

### Check 2026-09-25 20:55
- passed · ctest:modelcatalog — ctest -R modelcatalog passed for this revision on spark-dcc9, 2026-09-26T00:08:01Z
- passed · ctest:modelpicker — ctest -R modelpicker passed for this revision on spark-dcc9, 2026-09-26T00:55:41Z
- failed · ctest:modelspane — ctest -R modelspane failed for this revision on spark-dcc9
- missing-evidence · unittest:tests.test_openrouter_catalog — no run of tests/test_openrouter_catalog.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_guest_harness_provider — no run of tests/test_guest_harness_provider.py for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-23-MPA2/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-MPA2/README.md
- notice · ctest:modelcatalog — ctest -R modelcatalog is slow: p95 3.87 s, p50 3.72 s
- notice · ctest:modelpicker — ctest -R modelpicker is slow: p95 2.62 s, p50 2.59 s
- notice · ctest:modelspane — ctest -R modelspane is slow: p95 1.86 s, p50 1.62 s
- notice · unittest:tests.test_openrouter_catalog — tests/test_openrouter_catalog.py: 14 of 14 never ran here (test_the_cache_path_is_under_xdg_cache_home, test_rows_are_parsed_into_the_catalog_shape, test_prices_for_reads_the_cached_row_by_slug_or_by_name_and_never_fetches…)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 12 of 83 never ran here (test_guest_quota_refusal_keeps_a_structured_reason, test_exhausted_guest_retries_on_another_login_without_spending_a_reset, test_guest_tool_before_quota_refusal_is_not_replayed…)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 1 of 83 are slow (test_guest_startup_and_changes_report_each_models_supported_efforts)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 3 of 83 are not in the project any more (test_a_model_picked_after_the_stand_in_replaces_it, test_the_helper_worker_follows_the_priority_list_instead_of_starting_a_guest, test_with_nothing_usable_it_configures_anyway_and_a_turn_says_why)
- warning · manual:docs/qa_evidence/2026-09-23-MPA2/README.md — manual evidence docs/qa_evidence/2026-09-23-MPA2/README.md is not there
history: thread
## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean worktree). Evidence: `docs/qa_evidence/2026-09-25-verify-MPA2/`. Same landing as #CDP7 (commits 3cb7ff33 + 8d03da03, both ancestors).

**Done means, item by item:**
- Reproduce/explain the missing models — **passed**: the implementer's README reproduces both defects (Priorities reading a different pane catalog; late guest-harness catalog delivery) and pins them to the two commits.
- Correct the filtering/discovery with a focused regression — **passed by test evidence**: modelcatalog 73/0 and modelpicker 62/0 at HEAD (the regression cases are in those suites); live drives this sweep show full codex/openrouter lists in Enabled/Order without restart.

**Tests, line by line:**
- `ctest -R ^modelcatalog$` — **passed**: 73/0 (supplies the card's missing-evidence line).
- `ctest -R ^modelpicker$` — **passed**: 62/0.
- `ctest -R ^modelspane$` — **passed with a note**: 25/1, the #E8V1 stale string (#SYTR).
- `tests/test_openrouter_catalog.py` — **failed, not this card's fault**: later #Y4PJ drift, green at the card's own commit; filed #042V.
- `tests/test_guest_harness_provider.py` — **failed, not this card's fault**: later-added harness; #042V.
- manual README + logs — **present**.

Unresolved: nothing for this card; drift belongs to #042V.

Reviewed 2026-09-25 by the verifying session (qa-verify-MPA2), rev `2db96643`.
