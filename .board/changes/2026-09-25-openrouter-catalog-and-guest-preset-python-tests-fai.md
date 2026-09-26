---
id: 042V
type: work
status: inbox
labels: [bug, tests, backend]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [b759f23d], evidence: [docs/qa_evidence/2026-09-25-verify-bug-cards], related: [CDP7, Y4PJ], github: null}
---
# OpenRouter-catalog and guest-preset python tests fail at HEAD

## Issue
Two python suites that #CDP7 named are red at clean HEAD `2db96643` though both run green at CDP7's own last commit `78639301` (verified by checkout):

- `tests/test_openrouter_catalog.py::OpenRouterCatalogTests::test_catalog_rows_puts_the_built_in_tier_rows_first_then_the_live_ones` — `rows[0]["tier"]` is now `'flash'`, expected `'main'` (line 154). Introduced by `b759f23d` (#Y4PJ moved the openrouter main default onto z-ai/glm-5.3-flash) without updating the ordering expectation.
- `tests/test_guest_harness_provider.py::PresetTests::test_preset_rows` — the preset list now contains a third harness `guest:codex:ashe-ethz-ch` (line 151 still expects two).

Found during the models-verification sweep (card #CDP7); full commands in `docs/qa_evidence/2026-09-25-verify-bug-cards/NOTES.md` section C.

## Done means
Both suites run green at HEAD with expectations matching the shipped tables (or the table change is reverted).

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_openrouter_catalog tests.test_guest_harness_provider` — all pass.
