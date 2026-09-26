---
id: JQQF
type: work
status: done
labels: [bug, subagents, models]
assignee: agent
implemented_by: kimi/k3
verified_by: kimi/k3
resolution: done
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: optional, criteria: 'a fresh opus request on the GLM pane shows model claude opus on the spawned turn, not glm-5.3', sign_off: none, effort: low}
links: {plans: [], commits: [da8c681cb654, a7cc1fcce1c4], evidence: [], related: [7XN0], github: null}
---
# model "opus" on a non-Claude pane silently inherits the pane's model

## Issue
The agent tool's model words (opus/sonnet/haiku) are documented as selecting Claude Code, and #7XN0 (2026-09-23) made that work — but only when the pane itself is already a Claude guest. On any other pane the compatibility alias DEFAULT_ALIASES maps "opus" to "inherit", so SubagentFactory.choose() silently returns the pane's own model with no warning: on pane 22f05421 (main = glm-coding glm-5.3) every agent(..., model="opus") spawn ran GLM 5.3. The fix should resolve a Claude tier word to the Claude Code guest harness when the pane is not one (claude CLI installed, adapter available), and warn when no Claude harness can run it, instead of silently inheriting.

> "agents setting the model of the subagents isnt working. i requested opus here and got glm 5.3: 22f05421"
> — elliott · [session:2be62a45e1914208b543c085a572d86f](relay://session/2be62a45e1914208b543c085a572d86f) · 2026-09-25

## Done means
On a pane whose main model is not Claude (e.g. glm-coding glm-5.3, pane 22f05421), `agent(..., model="opus")` (and `agent_set_model`) runs the subagent on Claude Code's Opus — a `harness://claude` config with model `opus` — when this machine has the claude CLI and the adapter; the worker log shows `model_selection ... host=claude ... role=subagent` instead of glm-5.3. When no Claude harness can run here, the spawn still works on the pane's model and the tool reply carries a warning saying so — never a silent inherit. An explicit user alias for the word (`user_aliases`) still wins over all of this, and a Claude guest pane keeps the #7XN0 behaviour.

## Execution Summary
Landed `da8c681cb654` on main. `SubagentFactory.choose()` (backend/relay_core/subagents.py) now treats a Claude tier word (`opus`/`sonnet`/`haiku`, not overridden by a user alias) as "Claude Code": a Claude guest pane keeps #7XN0's behaviour; any other pane gets a `harness://claude` config with that model via the new `_claude_harness` helper when the claude CLI is installed, adapter available and signed in; otherwise it inherits the main model and the tool reply carries a warning instead of the old silent inherit. Spawn and `agent_set_model` already wire a guest config to a real guest provider, so `choose()` was the one fix point. Real-machine check on a glm-coding glm-5.3 base: `resolve('opus')` returns `harness://claude`/`opus`, no warnings.

## Tests
- `tests/test_subagents.py` — 57 passed on a clean export of the landed commit (`git archive da8c681cb654`), including two new cases: `test_opus_on_a_non_claude_pane_runs_the_claude_guest` and `test_claude_word_without_a_cli_warns_and_inherits`.
- `test_model_and_effort_resolution` (Kimi base) updated with explicit inherit aliases to pin the inherit path it was always about.
