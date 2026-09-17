# Implementer evidence: routing assist, thinking events, turn records, skills (backend)

Implementer: Claude Opus 5 (Claude Code, backend F2 worktree), 2026-09-17. Not a QA verdict.
Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` section 11 and the deviations in 11.1.

## Automated

`./scripts/test.sh`: 231 tests OK (25 new in `tests/test_routing_thinking_skills.py`, provider tests updated for
`thinking_delta`). Fake providers and local git repositories only.

## Live checks (stored keys from `relay_core.keystore`)

Scripts: `live_check.py` (`assist`, `refine`) and `live_thinking.py`. No keys are written to the logs.

### route_assist, 6 examples (`live-route-assist.log`)

Pane provider, `side_provider(cheap=True)` (low effort), no tools, max_tokens 256.

| Input | Kimi K3 (15 s limit) | GLM-5.3 Coding (15 s limit) |
|---|---|---|
| `go build ./...` | shell 0.99, 8.9 s | shell 0.98, 1.8 s |
| `go to the docs folder and summarize` | agent 0.95, 5.1 s | agent 0.97, 4.4 s |
| `make the tests pass` | agent 0.98, 5.5 s | agent 0.95, 11.7 s |
| `install ripgrep` | agent 0.70, 7.9 s | agent 0.90, 3.2 s |
| `find where the config is loaded` | agent 0.90, 5.0 s | agent 0.95, 2.0 s |
| `kill it` | agent 0.85, 2.9 s | agent 0.90, 2.4 s |

All 12 answers match the expected route. With the specified 2 s timeout: Kimi 0/6 answered (all `timeout`),
GLM 2/6 answered (`go build ./...` shell, `go to the docs…` agent) and 4 timed out. Each timeout returned
`route_assisted {route: null, error: "timeout"}` at 2000–2001 ms.

Output limit: the specified ~20 tokens truncated both providers (`max_tokens 20` rows). 64 tokens truncated Kimi on
`kill it` (`live-route-assist-max64.log`); 128 tokens truncated OpenRouter DeepSeek at low effort on 4/6
(`live-route-assist-max128-variants.log`), 256 answered 6/6 (`live-route-assist-openrouter-max256.log`). The
variants log also shows GLM with thinking disabled was not faster.

Local classifier on the same inputs: `go build ./...` shell without assist; the other five `needs_assist: true`
with local guess agent.

### Thinking events (`live-thinking.log`)

One turn each, "Is 391 prime? Answer in one short sentence."

- Kimi K3 (effort high): 21 `thinking_delta`, one `thinking_done {elapsed_ms: 3743, chars: 38}` before the first
  non-empty `delta`; `turn_summary {thinking_ms: 3743, elapsed_ms: 4075}` then `done`; all carry `turn_id`.
- GLM-5.3 Coding: 13 `thinking_delta` arriving in one burst at 9.4 s, `thinking_done {elapsed_ms: 9372}` before the
  answer. An earlier run that timed from the first reasoning chunk reported 0 ms, which is why elapsed now counts from
  the request (deviation in 11.1).
- Reasoning text never appeared in `delta`. Both providers send one empty-string `delta` before reasoning (existing
  behaviour, unchanged).

### refine_skills on a copy of a real skill (`live-refine.log`)

`~/.warp/skills/clean-commit` copied to a temp source dir, refined with Kimi into a temp target dir (33 s):
name kept (`clean-commit`), description unchanged in meaning (identical wording this time), `refined_from` points at
the copied original, the copy and the real `~/.warp` original are byte-identical to before, and the body was
reorganised into When to use / When not to use / Guardrails / Workflow (6 steps) / Commit message pattern
(4699 → 4973 bytes). The refined file itself is not committed (personal skill content).

## Not checked live

- `import_skills_preview`/`confirm`/`skills_check_updates` against a real https or ssh remote (tests use a local
  `file://` repository with the test-only flag). The git invocation is the same apart from the protocol allow-list.
- The GUI side of every event (no `src/` changes in this workstream).
