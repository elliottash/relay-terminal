---
id: XB7H
type: work
status: done
labels: [feature, models]
assignee: agent
implemented_by: kimi/kimi-k3
verified_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-22'
links: {plans: [], commits: [1aa4d54a, d870650f, 18dd9c71, '89893199', 6c7ede0a], evidence: [tests/test_presets.py], related: [MCP7], github: null}
---
# Rename models: opus 5 → opus 5.5, gpt-5.6-sol/luna → gpt-6-sol/luna

## Issue
replace opus 5 with opus 5.5

replace 5.6 sol with 6 sol, 5.6 luna with 6 luna

## Done means
- Relay's built-in catalogs, presets, rankings, guest fallbacks, docs and help text name the new models: Anthropic id `claude-opus-5-5` with the person-facing name `claude-opus-5.5` (the haiku/fable pattern: API hyphen, name dot), OpenRouter slug `anthropic/claude-opus-5.5`; `gpt-6-sol` and `gpt-6-luna` replace `gpt-5.6-sol`/`gpt-5.6-luna` (ids and names alike). `gpt-5.6-terra` is untouched.
- No live file (backend, src/, app/, docs/, tests/) mentions the old names; historical records (issues/, docs/qa_evidence/, tests/fixtures/issues_legacy/) keep them.
- Failure shows as: targeted pytest suites (test_presets, test_tier_lists, test_model_ranking, test_guest_*) or the affected ctest cases failing, or a leftover grep hit for the old names in live code.

## Plan
**Goal:** rename three built-in models everywhere live code, tests and living docs name them.

**Findings:** functional homes are `backend/relay_core/presets.py` (preset def, DEFAULTS, MODEL_CATALOG, GUEST_MODEL_ALIASES, OPENROUTER_TWINS, comments), `backend/relay_core/model-ranking.md` + `model_ranking.py` (name-keyed tables), `backend/relay_core/guest_harness_provider.py` (codex fallback list), the `/model` help string in `src/Pane.h`; everything else in src/, app/, docs/, backend is comments/examples. Tests and fixtures in tests/ assert the old names throughout.

**Steps:**
1. presets.py: id `claude-opus-5` → `claude-opus-5-5`, add `"name": "claude-opus-5.5"` to its MODEL_CATALOG row, twin `anthropic/claude-opus-5.5`, preset label `anthropic · claude opus 5.5`; `gpt-5.6-sol` → `gpt-6-sol`, `gpt-5.6-luna` → `gpt-6-luna`.
2. model-ranking.md / model_ranking.py: name-keyed rows → `claude-opus-5.5`, `gpt-6-sol`, `gpt-6-luna`.
3. Mechanical pass over remaining backend files, src/, app/, docs/: id contexts `claude-opus-5-5`, name/display contexts `claude-opus-5.5` / `Claude Opus 5.5`; sol/luna likewise; terra untouched.
4. tests/ and fixtures: same pass, fixing name-vs-id assertions (`model_name("guest:claude","opus")` → `claude-opus-5.5`).
5. Leftover grep for old names; run targeted tests; rebuild and run affected ctest cases.

**Risks:** name-vs-id divergence for opus (like haiku) means a blind sed is wrong; test failures will catch misclassifications. Owner-edited model-ranking.md gets renamed rows (that is the request).

**Verify:** `pytest tests/test_presets.py tests/test_tier_lists.py tests/test_model_ranking.py tests/test_guest_harness_provider.py tests/test_guest_harness_claude.py tests/test_guest_harness_codex.py tests/test_qa_verifiers.py tests/test_board_tools.py`, `node tests/model_name_peer.mjs`, and ctest cases modelcatalog/modelpicker/modelrows/modelspane/boardmodel/conversations/jobstab/filterpopup/sessioninfo.

## Execution Summary
- `1aa4d54a` renamed `claude-opus-5` → `claude-opus-5-5`, `gpt-5.6-sol`/`luna` → `gpt-6-sol`/`luna` across 70 live files (code, tests, fixtures, docs); `docs/qa_evidence/` and `issues/` left as the record. `BoardModel::modelLabel` now reads Anthropic's version dash as a dot ("Claude Opus 5.5", also fixes "Claude Haiku 4 5").
- `d870650f` + `18dd9c71`: `1aa4d54a` had swept another session's uncommitted edits into `tests/test_plan_turns.py`; taken back with `land.py repair`, and the rename re-landed alone.
- `89893199` (owner's follow-up): tier defaults high fable-5.1/astra, main opus-5.5/sol, flash sonnet-6/luna; `claude-sonnet-5` → `claude-sonnet-6`.
- `6c7ede0a`: person-facing name `claude-opus-5.5` (catalog `name`, ranking rows), OpenRouter twin `anthropic/claude-opus-5.5`; `Catalog::resolveKey` reads the version dash as a dot so Claude Code's reported `claude-opus-5-5` still resolves to `guest:claude|opus` (and `claude-haiku-4-5` to `|haiku`, which it already missed).
- Leftover grep for the old names over live files (excluding issues/, docs/qa_evidence/, tests/fixtures/issues_legacy/): no hits.

## Tests
`tests/test_presets.py`
`tests/test_tier_lists.py`
`tests/test_model_ranking.py`
`tests/test_guest_harness_claude.py`
`tests/test_guest_harness_provider.py`
`tests/test_qa_verifiers.py`
`tests/test_board_tools.py`
`tests/test_web_model_name.py`
`ctest -R modelcatalog`
`ctest -R board`

### Check 2026-09-23 15:04
- missing-evidence · unittest:tests.test_presets — no run of tests/test_presets.py for this revision, from any host, and no attached result
- not-applicable · unittest:tests.test_tier_lists — tests/test_tier_lists.py is not in the project any more
- missing-evidence · unittest:tests.test_model_ranking — no run of tests/test_model_ranking.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_guest_harness_claude — no run of tests/test_guest_harness_claude.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_guest_harness_provider — no run of tests/test_guest_harness_provider.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_qa_verifiers — no run of tests/test_qa_verifiers.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_board_tools — no run of tests/test_board_tools.py for this revision, from any host, and no attached result
- not-applicable · unittest:tests.test_web_model_name — tests/test_web_model_name.py is not in the project any more
- missing-evidence · ctest:modelcatalog — no run of ctest -R modelcatalog for this revision, from any host, and no attached result
- passed · ctest:board — ctest -R board passed for this revision on spark-dcc9, 2026-09-23T19:03:58Z
- notice · unittest:tests.test_tier_lists — tests/test_tier_lists.py is not in the project any more
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 8 of 71 never ran here (test_session_replacement_preserves_bridge_and_instructions, test_a_model_picked_after_the_stand_in_replaces_it, test_first_configuration_on_high_starts_guest_and_preserves_main…)
- notice · unittest:tests.test_qa_verifiers — tests/test_qa_verifiers.py: 39 of 39 never ran here (test_the_vendor_is_the_model_not_the_aggregator, test_each_preset_signs_with_its_own_vendor, test_a_guest_signs_the_model_it_ran_and_the_harness_that_ran_it…)
- notice · unittest:tests.test_board_tools — tests/test_board_tools.py: 1 of 258 never ran here (test_verifier_updates_and_qa_transition_preserve_the_implementer)
- notice · unittest:tests.test_board_tools — tests/test_board_tools.py: 1 of 258 are not in the project any more (test_the_offered_tools_are_read_only_files_search_and_the_modes_board_tools)
- notice · unittest:tests.test_web_model_name — tests/test_web_model_name.py is not in the project any more
- notice · ctest:modelcatalog — ctest -R modelcatalog is slow: p95 3.72 s, p50 3.72 s
- notice · ctest:board — ctest -R board is slow: p95 2.61 s, p50 2.56 s
history: thread
