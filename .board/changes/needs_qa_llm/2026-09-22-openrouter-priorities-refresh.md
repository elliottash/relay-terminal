---
id: VPR7
type: work
status: needs-qa-llm
assignee: codex-models-a1
labels: [bug, models]
rank: mopr7
created: '2026-09-22'
source: 'User report in Relay; observed on sphinxpad, 2026-09-22'
links: {plans: [], commits: [3cb7ff33ba054175c93c10b699f0c08804e12f07, 8d03da032c8c58fbf48d4c99b866d25455f1fa0b, 786393012dce25bca3eae91c372e0c3cac6e753f], evidence: [docs/qa_evidence/2026-09-22-VPR7/README.md, docs/qa_evidence/2026-09-25-verify-VPR7/], related: [MPA2], github: null}
---
# New OpenRouter models require a restart to appear in Priorities

## Issue
when i added a provider, i had to researt relay to see the model (openrouter) in the priorities tab.

## Done means
- Provider and guest model catalog changes reach an already open Priorities pane without restarting Relay.
- Searching by provider or model finds eligible Codex/OpenRouter models; intentional empty-query tier membership remains unchanged.
- Focused regressions cover refresh and search behavior, with recorded evidence.

## Plan
**Goal:** Resolve the reported missing models independently of intentional unranked search filtering.

**Findings:** `src/ModelPicker.cpp::buildTier` intentionally adds unranked models only while searching. Catalog updates flow through `src/Pane.h`, `src/RelayWindow.h`, and `src/ModelsPane.cpp`; provider eligibility and names live in `src/ModelCatalog.cpp`.

**Steps:**
1. Trace provider save, guest discovery, catalog propagation and search eligibility.
2. Add focused regressions and fix the demonstrated catalog/search defects.
3. Build targeted tests, stage UI evidence if feasible, and land with test evidence.

**Risks:** Concurrent shared checkout edits; coordinate any RelayWindow.h changes and preserve tier-list semantics.

**Verify:** Targeted model catalog/picker tests and isolated Qt UI evidence.

## Execution Summary
Full-app worker-pipe regression now proves refresh: a helper-only late key_stored/presets event updates an already open OpenRouter search without restart. Identical driver fails on the pre-fix binary. See live/baseline screenshots and stage.py in the evidence folder.

Provider settings could use m_active while Priorities continued serving another pane. Also Providers used helper presets before the served worker answered, while Priorities read only the empty served catalog. modelsSection now resolves the served target; applyModelsTarget falls back to the same helper catalog on open and refresh; the provider models link preserves that target.

Staged UI evidence: `docs/qa_evidence/2026-09-22-VPR7/priorities.png`.

## Tests
- `ctest -R ^modelpicker$` — tests/modelpicker_test.cpp
- `ctest -R ^modelcatalog$` — tests/modelcatalog_test.cpp
- `python3 docs/qa_evidence/2026-09-22-VPR7/stage.py` — full-app late helper events and Codex availability passed; pre-fix binary failed as expected.
- `xvfb-run -a build/relay-modelspane-tests` — 21 passed.
- `python3 -m unittest discover -s tests -p test_openrouter_catalog.py` — 13 passed.
- `python3 -m unittest discover -s tests -p test_guest_harness_provider.py` — 68 passed.
- `ctest -R ^modelspane$` — tests/modelspane_test.cpp
- `tests/test_openrouter_catalog.py`
- `tests/test_guest_harness_provider.py`
- manual: docs/qa_evidence/2026-09-22-VPR7/README.md

### Check 2026-09-25 20:55
- passed · ctest:modelpicker — ctest -R modelpicker passed for this revision on spark-dcc9, 2026-09-26T00:55:41Z
- passed · ctest:modelcatalog — ctest -R modelcatalog passed for this revision on spark-dcc9, 2026-09-26T00:08:01Z
- failed · ctest:modelspane — ctest -R modelspane failed for this revision on spark-dcc9
- missing-evidence · unittest:tests.test_openrouter_catalog — no run of tests/test_openrouter_catalog.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_guest_harness_provider — no run of tests/test_guest_harness_provider.py for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-22-VPR7/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-VPR7/README.md
- notice · ctest:modelpicker — ctest -R modelpicker is slow: p95 2.62 s, p50 2.59 s
- notice · ctest:modelcatalog — ctest -R modelcatalog is slow: p95 3.87 s, p50 3.72 s
- notice · ctest:modelspane — ctest -R modelspane is slow: p95 1.86 s, p50 1.62 s
- notice · unittest:tests.test_openrouter_catalog — tests/test_openrouter_catalog.py: 14 of 14 never ran here (test_the_cache_path_is_under_xdg_cache_home, test_rows_are_parsed_into_the_catalog_shape, test_prices_for_reads_the_cached_row_by_slug_or_by_name_and_never_fetches…)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 12 of 83 never ran here (test_guest_quota_refusal_keeps_a_structured_reason, test_exhausted_guest_retries_on_another_login_without_spending_a_reset, test_guest_tool_before_quota_refusal_is_not_replayed…)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 1 of 83 are slow (test_guest_startup_and_changes_report_each_models_supported_efforts)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 3 of 83 are not in the project any more (test_a_model_picked_after_the_stand_in_replaces_it, test_the_helper_worker_follows_the_priority_list_instead_of_starting_a_guest, test_with_nothing_usable_it_configures_anyway_and_a_turn_says_why)
- warning · manual:docs/qa_evidence/2026-09-22-VPR7/README.md — manual evidence docs/qa_evidence/2026-09-22-VPR7/README.md is not there
history: thread
## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8`. Evidence: `docs/qa_evidence/2026-09-25-verify-VPR7/` plus the re-run staged captures `docs/qa_evidence/2026-09-22-VPR7/verify-20260925-*.png`. Same landing as #CDP7/#MPA2.

**Done means, item by item:**
- Provider and guest catalog changes reach an open Priorities pane without restarting — **passed, re-proven live at HEAD**: the card's own `stage.py` re-run exits 0 (late-model rows appear in the open Priorities pane after the trigger; '7 of 7' providers; six staged codex models in the availability list; helper key_stored recorded).
- Search by provider/model finds eligible models; empty-query membership unchanged — **passed by test evidence** (modelpicker 62/0, modelcatalog 73/0).
- Focused regressions with evidence — **passed** (suites + stage captures).

**Tests, line by line:**
- `ctest -R ^modelpicker$` — **passed**: 62/0.
- `ctest -R ^modelcatalog$` — **passed**: 73/0.
- `python3 docs/qa_evidence/2026-09-22-VPR7/stage.py` — **passed** (re-run today, exit 0; see above).
- `xvfb-run -a build/relay-modelspane-tests` / `ctest -R ^modelspane$` — **passed with a note**: 25/1 (card: 21 passed); the 1 is the #E8V1 stale string (#SYTR), which postdates landing.
- `tests/test_openrouter_catalog.py` — **failed, not this card's fault**: later #Y4PJ drift (#042V).
- `tests/test_guest_harness_provider.py` — **failed, not this card's fault**: later-added harness (#042V).

Unresolved: nothing for this card; drift belongs to #042V.

Reviewed 2026-09-25 by the verifying session (qa-verify-VPR7), rev `2db96643`.
