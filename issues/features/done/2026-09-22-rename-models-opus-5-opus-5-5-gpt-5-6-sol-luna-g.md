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
