---
id: VTJR
type: work
status: inbox
labels: [bug, agent, subagents]
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'launch one research subagent with model opus and effort high from a GLM main pane; its reply names the limit if it hits one, and the log''s turn_start says the model that ran', sign_off: none, effort: low}
source: 'Claude Fable guest session in Relay, 2026-09-24, while running the #9HS0 research'
links: {plans: [], commits: [], evidence: [], related: [2CZP, 9HS0], github: null}
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
- A subagent run that ends at its step or tool-call budget is not reported as `done`: the `agent`, `agent_wait` and delivered results say it stopped at the limit and that `agent_message` resumes it, and the result text begins with that note rather than with the model's last remark. A unit test drives a fake provider past the budget and asserts the reply.
- The `general` definition's default budget is no longer 12 steps: it follows the pane's backstop policy from #2CZP, and a run that fetches twenty pages finishes in one run instead of three resumes.
- Asking for `model: "opus"` from a pane whose main model is not a Claude guest either runs on Claude Code (when it is installed and logged in) or returns the model warning in the tool reply; the worker log's `turn_start` line names the model that actually ran.
- Failure is recognised by: an `agent_wait` result of `status: done` whose text ends in a colon or "Now I'll…" with `stop_reason=limit` in the log; or a `turn_start` line for a subagent reading `max_steps=12` after the change.
