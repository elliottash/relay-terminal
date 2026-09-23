---
id: Y4PJ
type: work
status: inbox
labels: [bug, models]
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-22'
source: 'found by scripts/relay-models.py check while delivering #MCP7, 2026-09-22'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-MCP7/check.txt], related: [MCP7], github: null}
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
