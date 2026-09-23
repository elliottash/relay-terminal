---
id: MCP7
type: work
status: needs-verification
labels: [feature, models]
assignee: claude-code
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-22'
source: Claude Code (opus) in a Relay pane, 2026-09-22
links: {plans: [], commits: [6c7ede0a, ec29ae5c, 263b28b4, 479bc9d5, ca578956], evidence: [docs/qa_evidence/2026-09-22-MCP7/], related: [XB7H, Y2JW, Y4PJ], github: null}
---
# One command to refresh the model defaults when new models ship

## Issue
and make it easy to refresh defaults with new models. we can do that with my current set up, and then we need a way to make it easily repeatable

## Done means
- `scripts/relay-models.py discover` lists, per provider this machine can reach (stored keys, installed codex/claude, OpenRouter's public listing), the models it serves that Relay's catalog does not know, and catalog models it no longer serves — without printing a key.
- `scripts/relay-models.py set <provider> high=… main=… flash=…` rewrites every place a provider's defaults live (`model-ranking.md` classes, `presets.TIER_DEFAULTS`, `MODEL_CATALOG` rows and tier tags) in one step; `rename <old> <new>` does the repo-wide id/name rename (live code, tests, docs; not `issues/` or `docs/qa_evidence/`).
- `scripts/relay-models.py check` exits non-zero when those places disagree, and a unit test runs it, so drift fails the tests.
- The procedure is written once, in `model-ranking.md`'s "How to edit", and the 2026-09-22 refresh (Opus 5.5, Sonnet 5 kept, GPT-6 Sol/Luna) is consistent under `check`, including Opus's person-facing name `claude-opus-5.5` (#XB7H). (Corrected 2026-09-22: this line said "Sonnet 6" before the owner's "claude sonnet 5, not 6".)
- Failure shows as: `check` passing while `model-ranking.md` and `TIER_DEFAULTS` name different models, `set` leaving one of the places unchanged, or `discover` failing on a provider with no key instead of skipping it.

## Plan
**Goal.** Refreshing the defaults for new models becomes: `discover` (what is new), `rename`/`set` (apply), `check` + targeted tests (prove), `land.py` (commit).

**Findings.** A provider's defaults live in four places that drift apart by hand: the `classes` column of `backend/relay_core/model-ranking.md` (what the default lists are built from), `presets.TIER_DEFAULTS` (per-provider tier model and its request extras), `presets.MODEL_CATALOG` (model rows, `name` overrides, `tier` tags), and `presets.OPENROUTER_TWINS`/`GUEST_MODEL_ALIASES`. The 2026-09-22 refresh touched all four plus ~70 files of tests and docs naming the models, and left Opus's name inconsistent with Haiku/Fable (`claude-opus-5-5` instead of `claude-opus-5.5`).

**Steps.**
1. Opus name: `MODEL_CATALOG` row `name: claude-opus-5.5`, OpenRouter twin `anthropic/claude-opus-5.5`, name-keyed rows in `model-ranking.md`, and the tests that assert the name (#XB7H's Done means).
2. `scripts/relay-models.py` with `show`, `check`, `discover`, `set`, `rename`, and `--root` so tests run it on a copy; `tests/test_relay_models.py`.
3. "Refreshing the models" in `model-ranking.md`'s How to edit.
4. Run `discover` on this machine and record what it finds.

**Risks.** `set` edits Python source by pattern; it refuses rather than guesses when a pattern does not match. `rename` cannot tell an id from a name in free text, so it takes both spellings explicitly and the tests are the backstop.

**Verify.** `tests/test_relay_models.py`, `test_model_ranking`, `test_presets`, `test_tier_lists`; `scripts/relay-models.py check` on the tree; a `discover` run saved as evidence.

## Tests
`tests/test_relay_models.py`
`tests/test_model_ranking.py`
`tests/test_presets.py`
`tests/test_tier_lists.py`

## Execution Summary
- `scripts/relay-models.py` (`ec29ae5c`, written by an Opus subagent, reviewed here): `show` (each provider's high/main/flash/lite from the ranking beside `TIER_DEFAULTS`, `*` on a disagreement), `check` (exit 1 per disagreement between `model-ranking.md`, `TIER_DEFAULTS` and `MODEL_CATALOG` tier tags, plus `model_ranking.check()`), `discover` (read-only, per provider: OpenRouter's public listing, the codex CLI catalogue, Claude Code's aliases, and `GET /models` for each preset with a stored key; NEW and GONE; never prints a key; `--json`), `set <provider> CLASS=MODEL…` (ranking classes, `TIER_DEFAULTS`, catalog rows/tier tags together; refuses and writes nothing on an unmatched pattern; `--dry-run` diff), `rename OLD NEW [--name a=b]` (tracked files, skipping `issues/`, `docs/qa_evidence/`, `tests/fixtures/issues_legacy/`). Every writing command prints the `land.py begin` line. `tests/test_relay_models.py`: 19 tests, offline, on a copy via `--root`.
- "Refreshing the models" in `backend/relay_core/model-ranking.md`: discover → rename → set → check/show + tests → land.
- The 2026-09-22 refresh itself, done on this card: Opus named `claude-opus-5.5` with `Catalog::resolveKey` reading Anthropic's version dash as a dot (`6c7ede0a`); Sonnet stays `claude-sonnet-5` (owner: "claude sonnet 5, not 6", `263b28b4`); gpt-6-sol starts main at medium (owner, `479bc9d5`); the openai preset runs gpt-6-sol at 1,050,000/128,000 (owner, `ca578956`).
- `discover` on this machine (evidence `discover.txt`): OpenRouter serves `anthropic/claude-opus-5.5`, `openai/gpt-6-sol`, `openai/gpt-6-luna`, `anthropic/claude-sonnet-5` (no sonnet-6); new since the catalog: `gpt-6-{astra,sol,luna}-pro`, `glm-5.3-flashx`, `gemini-3.7-flash`, `kimi-k2.6`/`kimi-k2.7-code`, and codex still lists `gpt-5.6-sol`/`gpt-5.6-luna`/`gpt-5.5` beside the 6s.
- `check` reports three disagreements that predate this card (gemini main/flash, openrouter main) — filed as #Y4PJ with a recommendation; `tests/test_relay_models.py` pins them in `KNOWN_DRIFT`.
