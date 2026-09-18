---
id: OT32
type: work
status: needs-qa-llm
labels: [change]
component: [gui, providers]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'With no `provider/max_tokens` in relay.conf, Settings › Models shows "Output token limit" 32768 and the agent requests `max_tokens: 32768`; a saved value is kept; `ctest` and `./scripts/test.sh` pass'
source: 'owner, 2026-09-18: "change the max output token default to 32K"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# The output token limit defaults to 32K

## Report

The owner asked for a default of 32K output tokens per model call. It was 8192.

## Change

The default is now **32768** — the codebase writes token sizes as powers of two (8192, 32768), and
32768 was already the top of the allowed range (256–32768), so the default is also the maximum.
Every place that supplied the old default now supplies the new one:

- `src/main.cpp`: the Settings pane row "Output token limit", the provider dialog's spin box, and
  the three places that read `provider/max_tokens` with a fallback (a pane's configure, its
  set_model on a model switch, and the Switchboard worker's configure).
- `backend/relay_core/provider.py`: `ProviderConfig.max_tokens`.
- `backend/relay_core/session_protocol.py`: the fallback when a `configure`/`set_model` request has
  no `max_tokens`.
- `backend/relay_core/context.py`: `ContextTracker`'s default (the agent always passes the real
  value; this keeps the two consistent).
- `docs/ARCHITECTURE.md`: states the default.

A saved `provider/max_tokens` is untouched: only a missing value falls back. Someone who changed
the setting, or pressed OK in Advanced provider settings (which saves the spin box's value even when
it was left alone), keeps what was saved — 8192 for most existing installs — until they change it.

Side calls are unaffected: "cheap" calls still cap at 4096, and titles, route assist, key tests
and audits keep their own small limits.

No model in the presets has a documented output cap below 32K (Kimi K3 131K default, GLM-5.3
128K, DeepSeek V4.1 Flash 384K; see `docs/INTAKE-CLARIFICATION-RESEARCH.md`). Relay has no
per-model output cap, so nothing clamps: a custom endpoint with a smaller output limit (a local
model, or an OpenRouter route such as DeepInfra's Kimi at 16,384) can reject the request and
needs a lower setting. The compaction threshold already reserves `max_tokens` (`limit_tokens`), so
on a small context window auto-compaction starts earlier than before.

Files: `src/main.cpp`, `backend/relay_core/provider.py`, `backend/relay_core/session_protocol.py`,
`backend/relay_core/context.py`, `tests/test_provider.py`, `docs/ARCHITECTURE.md`. No protocol
change.

## QA checklist

1. Fresh `XDG_CONFIG_HOME`: Settings › Models shows "Output token limit" 32768; Advanced provider
   settings shows 32768.
2. Point the provider at a local mock endpoint (`http://127.0.0.1:<port>/v1`) and send a prompt:
   the request body carries `"max_tokens": 32768`. Same after switching model with the chip.
3. Set the limit to 4096, restart: it stays 4096.
4. `tests/test_provider.py` `test_the_output_token_limit_defaults_to_32k` passes.
