---
id: VTJR
type: work
status: needs-verification
labels: [bug, agent, subagents]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: aa03e45a-c6a4-4332-a7a7-acee4cca3b28
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'launch one research subagent with model opus and effort high from a GLM main pane; its reply names the limit if it hits one, and the log''s turn_start says the model that ran', sign_off: none, effort: low}
source: 'Claude Fable guest session in Relay, 2026-09-24, while running the #9HS0 research'
links: {plans: [], commits: [946ec3b8, b64b01c6, 2c7c7774], evidence: [tests/test_subagents.py], related: [2CZP, 9HS0], github: null}
---
# A subagent that hits its 12-step limit is reported as done, with its last progress remark as the report, and "opus" silently ran on glm-5.3

## Issue
write a card describing this subagent resume issue

## Discussion points
**What was seen.** Four research subagents launched from a Claude Code guest pane with `model: "opus"`, `effort: "high"`, `background: true` (the #9HS0 research, 2026-09-24). Each `agent_wait` came back `status: done` with a result that was a mid-work remark rather than a report: *"Bing works with URL decoding. Now I'll batch-fetch the Warp and Ghostty sources."*, *"Bing has degraded to junk results. Let me try the Jina reader…"*, *"Excellent — the sitemap gave me exact NN/g URLs… Fetching those plus Canon Cat and UserOnboard:"*, or the literal `(no final report)`. Each agent needed three `agent_message` resumes before a report existed; the caller, reading the remarks as the guest ending its turn on narration, told them to stop narrating, which was the wrong diagnosis.

**What the worker log says** (`~/.local/share/relay/logs/worker.log`, pane `3b50a864`, turns `t9deb33a4-1…14`). Every first run and most resumes ended at the step budget, and none ran on Opus:

| turn | model | tools | ms | end |
|---|---|---|---|---|
| -1 (a1 first run) | glm-5.3 | 22 | 71,254 | `outcome=done stop_reason=limit` |
| -2 (a2) | glm-5.3 | 22 | 211,143 | `stop_reason=limit` |
| -3 (a3) | glm-5.3 | 13 | 225,743 | `stop_reason=limit` |
| -4 (a4) | glm-5.3 | 23 | 201,810 | `stop_reason=limit` |
| -5, -6, -7 (first resume) | glm-5.3 | 23, 12, 23 | 185 s, 213 s, 366 s | all `stop_reason=limit` |
| -9, -10, -11 (second resume) | glm-5.3 | 12, 23, 22 | 334 s, 417 s, 85 s | all `stop_reason=limit` |
| -8, -12, -13, -14 (the runs told to write the file first) | glm-5.3 | 15, 1, 5, 3 | 899 s, 196 s, 710 s, 888 s | `outcome=done`, no limit |

Every `turn_start` line reads `model=glm-5.3 host=api.z.ai effort=high max_steps=12`. Eleven of fourteen runs hit the limit; the ones that finished were the ones whose instruction was to write the file in a single tool call.

**Three faults.**

1. **A subagent's step budget is 12 while a pane's is 500.** `AgentDefinition.max_steps` defaults to 12 (`backend/relay_core/agents_defs.py:80`), clamped by `MAX_STEPS = 50` (`:38`); `SubagentFactory` builds the agent with `max_steps=min(definition.max_steps, MAX_STEPS)` and `max_tool_calls=max(24, 3*steps)` (`backend/relay_core/subagents.py:323-337`). #2CZP uncapped the pane's turn to 500 steps and 2000 tool calls as a backstop fuse, with loop detection doing the real stopping; the subagent never got that decision. `effort: "high"` changes the provider's reasoning setting and nothing about the budget. A research task that fetches pages needs more than twelve model steps by construction.

2. **Hitting the limit is reported as `done`, and the report is whatever the model last said.** `Agent._stop_at_limit` appends the note *"Stopped at the turn limit (12 of 12 model steps…). The request is not finished; ask the agent to continue."* as a **user-role** message (`backend/relay_core/agent.py:3786-3792`) and records `stop_reason="limit"`. `SubagentManager._run` maps `sub.outcome == "done"` to outcome `done` regardless (`subagents.py:889-896`), and `_final_text` returns the last non-empty **assistant** message (`:904-910`), so the caller receives a progress remark, or `(no final report)` when the last step held only tool calls. `SubagentManager.result` carries `id, type, status, tools, tokens, elapsed_ms, result` and no `stop_reason` (`:1064-1071`). Nothing the caller can see distinguishes "finished and this is the report" from "cut off at step 12".

3. **`model: "opus"` fell through to the main model with no warning in the tool result.** `SubagentFactory.choose` maps `opus` to the Claude Code guest only when the base config is already a Claude guest (`subagents.py:257-261`); otherwise `opus` is not a preset, `choose` appends the warning *"model 'opus' is not a Relay preset; using the main model"* (`:276`) and returns the base, here glm-5.3. The warning is put on the `subagent_started` **event** (`:756-757`), not on the `agent` tool's reply, which was `{"id": "a1", "type": "general", "status": "running", …}`. The tool's own description says *"opus for Claude Code Opus"* (`:514`, `:530`), and Claude Code was installed and logged in on this machine. The owner had asked in words for Opus subagents and got GLM.

**Proposed fix**, in the order it pays:

- `result` (and the `agent_wait` / foreground `agent` reply) carries `stop_reason`, and a run that stopped at the limit reports `status: "limit"` (or `done` with `stopped_at_limit: true`), with the result text starting with the limit note itself: *"Stopped at the turn limit (12 of 12 model steps). Not finished; agent_message resumes it."* followed by the last assistant text. The todo Relay keeps for the subagent should not read completed either.
- The `general` definition's budget follows #2CZP: the clamp maxima as a fuse (raise `MAX_STEPS` from 50, default from 12), with loop detection and the cadence recitation doing the stopping, as they do for the pane. If a per-definition small budget is wanted for chores, keep it on those definitions, not on `general`.
- `choose()` warnings are returned in the `agent` and `agent_set_model` replies, so a caller who asked for a model that was not honoured sees it at the call. And `opus` resolves to the Claude Code guest whenever Claude Code is installed and logged in (`guest_harness_provider.installations()` / `login_status`), not only when the base config already is Claude.

## Done means
- Every builtin subagent definition — `general` **and** `signal` — runs on the pane's #2CZP backstop: `max_steps` 500 with the 4:1 tool-call fuse. No subagent definition keeps a 12-step budget (owner decision, 2026-09-24).
- A run that ends at a budget is reported as `status: "limit"` with `stop_reason` and a leading limit note in the result text — never `done` with a progress remark — and `agent_message` resumes it; the subagent's todo goes back to pending, not completed.
- `model: "opus"` from a non-Claude pane returns the fallback warning in the `agent` / `agent_wait` / `agent_set_model` reply; the worker log's `turn_start` names the model that ran.
- Failure is recognised by: any builtin definition (or any subagent `turn_start` line) still reading `max_steps=12`; or an `agent_wait` result of `status: done` whose text is a mid-work remark.

## Execution Summary
Fixed in 946ec3b8 (and b64b01c6, landed alongside by the gate); the owner's no-12-step decision finished in 2c7c7774.

1. **Budget.** `AgentDefinition.max_steps` default 12 → 500 (the pane's #2CZP backstop) and `MAX_STEPS` 50 → 500 as the pure fuse; the factory's tool-call ratio follows the pane's 4:1. **2c7c7774:** the builtin `signal` definition's trailing `12` is now `MAX_STEPS` too, per the owner's 2026-09-24 decision, so no builtin subagent definition keeps a 12-step budget.
2. **Limit reporting.** `Subagent` now keeps the done event's `stop_reason`; a run that ended on the limit maps to status `"limit"` (new in the vocabulary), the result dict carries `stop_reason`, and the result text **begins** with "Stopped at the turn limit before it finished; send agent_message to resume it." — the trailing assistant remark follows as data, so a colon-ending progress note can no longer pass as a report. `todos.subagent_finished` puts a limit-stopped todo back to **pending** with that note, never completed. The Subagents panel has a `◔` icon for the new status.
3. **Warnings.** `agent` (foreground and background), `agent_wait` and `agent_set_model` replies now carry the factory's `warnings` list, so `model: "opus"` from a GLM pane tells the caller at the call that it ran on the main model. Deliberately not done: auto-resolving `opus` to the Claude Code guest from any pane — that spawns a guest harness child from an arbitrary base config and is a feature of its own; the warning makes the fallback visible, which was the reported harm.

`rg` for `max_steps=12|12-step|12 model steps|None, 12)` over backend/docs finds only `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md`, a historical research note on the pane's old limit, left as the record.

## Decisions
**2026-09-24, owner:** "i dont want the 12 step limit any more, give subagents the same limit as main agents" — no subagent keeps a 12-step budget. 946ec3b8 already gave `general` the pane's 500-step #2CZP backstop; the decision also removes the 12 on the `signal` definition (`agents_defs.py`, the one place it remains), so signal pickup runs get the same limit as main agents. Loop detection and the tool-call fuse do the stopping, as they do for the pane.

## Plan
**Goal.** Land the owner's 2026-09-24 decision: remove the last 12-step budget — the `signal` definition's — so every subagent runs on the same limit as a main agent. The rest of the card (limit reported as `limit`, warnings in tool replies, `general` at 500) landed in 946ec3b8 and is not reopened.

**Findings.**
- `backend/relay_core/agents_defs.py:126` — the `signal` `AgentDefinition(...)` ends with the positional `12`; `general` ends with `MAX_STEPS` (= 500, `:40`); the dataclass default is already 500 (`:85`).
- `backend/relay_core/subagents.py:323-336` builds every subagent with `steps=min(definition.max_steps, MAX_STEPS)`, `max_tool_calls=max(24, 4*steps)` — nothing else special-cases signal's budget.
- `tests/test_subagents.py:953` `test_general_definition_follows_the_pane_backstop` asserts only `general`; no test asserts signal's 12. (`tests/test_agent.py:596`'s `max_steps=12` is a local fake budget for a loop-detection test, unrelated.)

**Steps.**
1. In `agents_defs.py`, change the `signal` definition's trailing positional `12` to `MAX_STEPS`, mirroring `general`.
2. Extend the budget test to assert the **builtin** `signal` definition's `max_steps` is 500 too (read it through the catalog, so user config cannot mask the builtin).
3. `rg -n --hidden 'max_steps=12|, 12\)|12-step|12 model steps' backend tests docs .board` and fix any doc, test or string that still promises signal a small budget — including this card's Execution Summary item 1, whose "keeps its explicit small budget, per the card" line the decision overrode.
4. Update the Execution Summary to record the signal change and its commit.

**Risks.** A runaway signal pickup can now burn up to 500 steps instead of 12. That is the same trade the pane made in #2CZP, and the same guardrails apply (loop detection, 4:1 tool-call fuse). A signal-specific cost cap would be a new owner decision, not this card. No open question.

**Verify.** `pytest tests/test_subagents.py tests/test_signals.py tests/test_agent.py -q` (the touched suites, plus signals for the definition change), then land via `python3 scripts/land.py` per the repo rules. Post-land probe: a subagent `turn_start` line — a signal run included — reads `max_steps=500`, never 12.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_subagents tests.test_signals tests.test_signal_threads tests.test_agent` — 203 tests, OK (2026-09-24, working tree at 2c7c7774). pytest is not installed here, so unittest ran them.
- New: `test_every_builtin_definition_follows_the_pane_backstop` asserts `{general: 500, signal: 500}` over `BUILTINS`.
- From 946ec3b8: `LimitReportingTests` (status `limit`, `stop_reason`, leading note, `agent_message` resumes), `test_spawn_warnings_reach_the_tool_reply`, and the limit row in tests/test_todo_subagents.py.
- Not done here: the live probe (a subagent `turn_start` reading `max_steps=500`, and an `opus` spawn from a GLM pane) — that is for the verifier.
