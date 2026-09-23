---
id: Y4PJ
type: work
status: done
labels: [bug, models]
assignee: agent
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-22'
source: 'found by scripts/relay-models.py check while delivering #MCP7, 2026-09-22'
links: {commits: [b759f23deb4f09095716f4fb5a36d39b9f50b3f2], evidence: [docs/qa_evidence/2026-09-22-MCP7/check.txt], github: null, plans: [], related: [MCP7, D0MC]}
---
# TIER_DEFAULTS disagrees with model-ranking.md for gemini main/flash and openrouter main

## Issue
`python3 scripts/relay-models.py check` on main at ca578956 (docs/qa_evidence/2026-09-22-MCP7/check.txt):

    openrouter main: TIER_DEFAULTS names deepseek/deepseek-v4.1-flash (deepseek-v4.1-flash), but model-ranking.md gives z-ai/glm-5.3-flash (glm-5.3-flash) — its Provider picks main cell is glm-5.3-flash
    gemini main: TIER_DEFAULTS names gemini-3.1-pro-preview (gemini-3.1-pro-preview), but model-ranking.md gives gemini-flash-latest (gemini-flash-latest) — that is its highest-scored main model
    gemini flash: TIER_DEFAULTS names gemini-3.8-flash (gemini-3.8-flash), but model-ranking.md gives gemini-flash-latest (gemini-flash-latest) — that is its highest-scored flash model

The default priority lists follow model-ranking.md; `presets.TIER_DEFAULTS` (a provider's per-tier model and request extras) still names the older picks. Predates #MCP7; `tests/test_relay_models.py` lists the three in `KNOWN_DRIFT` so they fail loudly once fixed.

Recommendation: the ranking file is the owner's written choice, so point TIER_DEFAULTS at it —
`scripts/relay-models.py set gemini main=gemini-flash-latest flash=gemini-flash-latest` and
`set openrouter main=z-ai/glm-5.3-flash` — then drop the three KNOWN_DRIFT lines. It changes which
Gemini model a Gemini pane starts on (3.1 Pro preview → the Flash alias), which is the owner's call.

## Decisions
- "Follow the file" (owner, 2026-09-23, asked which way to resolve the TIER_DEFAULTS ↔ model-ranking.md drift): the ranking file wins; TIER_DEFAULTS is set to its picks and the three KNOWN_DRIFT prefixes are deleted. Keeping the old picks or leaving the drift known were rejected.

## Done means
`python3 scripts/relay-models.py check` prints `ok` (no drift lines at all), and `PYTHONPATH=backend python3 -m unittest tests.test_relay_models` is green. `presets.TIER_DEFAULTS` names gemini-flash-latest for gemini main and flash and z-ai/glm-5.3-flash for openrouter main; `KNOWN_DRIFT` in tests/test_relay_models.py is empty. Failure would show as any of the three drift lines still in check output or the suite still red.

## Execution Summary
- Owner's decision applied: `presets.TIER_DEFAULTS` gemini main/flash → `gemini-flash-latest`, openrouter main → `z-ai/glm-5.3-flash`. MODEL_CATALOG gained the openrouter row `z-ai/glm-5.3-flash` displaying as `glm-5.3-flash` (mirroring relay-pro's row), and the deepseek/deepseek-v4.1-flash row's tier moved main → flash, which now names it alone.
- `backend/relay_core/model-ranking.md` is untouched: the `set` tool had normalized its Provider-picks cell to the slug and added a slug row, both reverted to HEAD — the file is the owner's, written in display names, and presets' names-to-slug map already resolves `glm-5.3-flash` → `z-ai/glm-5.3-flash`.
- `KNOWN_DRIFT` emptied in tests/test_relay_models.py; the two tests that encoded the superseded defaults (test_owner_requested_defaults, test_tier_rows_match_tier_defaults) updated with the 2026-09-23 decision noted inline.
- Proven by `check` → `ok` (exit 0) and the four modules `set` names: 154 tests with only the three pre-existing relay-pro StartEffortTests failures, identical at HEAD baseline and filed separately.

## Tests
`python3 scripts/relay-models.py check` — `ok`, exit 0 (three drift lines at HEAD).
`PYTHONPATH=backend python3 -m unittest tests.test_relay_models tests.test_model_ranking tests.test_presets tests.test_tier_lists` — 154 tests, 3 pre-existing relay-pro failures (same at HEAD).
