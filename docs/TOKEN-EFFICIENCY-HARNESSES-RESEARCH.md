# Token efficiency in Codex, OpenCode and Claude Code

Research checked 2026-09-22. This compares current public documentation with Relay's
implementation. It is a proposal, not a claim that another harness's behavior can be
controlled by Relay. OpenCode details below refer to **V2**; its V1 compaction settings
and pruning behavior differ.

## The problem to solve

The local 36-hour Codex usage audit (2026-09-21 09:40 to 2026-09-22 21:40 EDT)
found about 1.03 billion text tokens across 9,078 model responses and 143 threads.
About 97% of input was cached, yet the mean response still carried roughly 113,000
input tokens. Applying the published ChatGPT credit rates yields an **estimate** of
about 32,800 credits; it is not the account's bill, and it excludes image-generation
charges. GPT-6 Astra and many parallel, long-running panes drove most of it. This
measurement makes the first objective *fewer repeated context tokens per useful
outcome*, followed by a cheaper model where it can do the job. Improving the cache-hit
percentage alone is insufficient: cached input is still charged on every call.

For scale, 1,000 Astra calls each rereading a 100,000-token cached prefix cost about
2,500 credits at the published 25 credits/million cached-input rate. Reducing that
prefix to 20,000 tokens would make the same 1,000 calls about 500 credits, before
counting compaction overhead or possible cache rebuilds. This is a model of one cost
component, **not** a predicted saving for Relay. [ChatGPT/Codex credit rates](https://learn.chatgpt.com/docs/pricing);
[OpenAI usage and caching guidance](https://developers.openai.com/api/docs/guides/agents-api/observability).

## What the harnesses actually do

| Concern | Codex | OpenCode V2 | Claude Code |
|---|---|---|---|
| Long history | The OpenAI-managed Codex harness supports automatic compaction; the Responses API supports a configurable token threshold and an opaque checkpoint. | Auto-compaction is on by default. It estimates prompt + messages + tools before each call, keeps about 15k recent tokens with a structured checkpoint, reserves 20k, and retries a clean overflow once. | Auto-compacts near the context limit; `/compact` can specify what to preserve. Recommends `/clear` between unrelated tasks because compaction itself reads the old context. |
| Cache | Reuse requires an identical rendered prefix. Stable instructions and tool schemas, appended turns, and tool definitions loaded at the end preserve more cache. Cached tokens still cost money. | A checkpoint replaces older active context; prior messages remain stored. Native provider compaction is optional per provider/model; it falls back to a local checkpoint on qualifying overflow. | `/usage` reports cache reads/writes and, on supported versions, cache misses and likely causes. It distinguishes expected rebuilds after compaction or clearing old tool results. |
| Tools and output | Tool search defers schemas until used and appends them to the context. Programmatic tool calling can reduce many intermediate results to one result; command output has a truncation budget in the open-source CLI. | Recent large tool results are shortened in the checkpoint; old messages remain available outside active model context. | MCP schemas are deferred by default. Docs recommend filtering logs/test output with hooks, narrow code navigation, and using a subagent so only a summary returns. |
| Delegation | Each subagent makes independently billable model calls; its usage should be counted with root work. | Child agents use their own session/context and can have a different model. | Subagents isolate context, but agent *teams* multiply it. Docs advise focused prompts, cheaper teammate models, small teams, and stopping them promptly. |
| Visibility | Session/turn usage records expose input, cached input, output and subagent IDs, but the counts are best-effort. | Compaction exposes its configured budget and checkpoint events. | `/usage` separates session/model costs, plan attribution and cache statistics; `/context` shows what occupies the window. |

Sources: [Codex harness/Agents API](https://developers.openai.com/api/docs/guides/agents-api/overview),
[OpenAI compaction](https://developers.openai.com/api/docs/guides/compaction),
[OpenAI prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching),
[OpenAI tool search](https://developers.openai.com/api/docs/guides/tools-tool-search),
[OpenAI programmatic tool calling](https://developers.openai.com/api/docs/guides/tools-programmatic-tool-calling),
[Codex CLI tool-output source](https://github.com/openai/codex/blob/main/codex-rs/core/src/tools/context.rs),
[OpenCode V2 compaction](https://opencode.ai/v2/docs/compaction),
[OpenCode V2 agents](https://opencode.ai/v2/docs/agents),
[Claude Code context costs](https://code.claude.com/docs/en/features-overview),
[Claude Code cost guidance](https://code.claude.com/docs/en/costs).

These are not all the same product boundary. Codex's managed Agents API describes
the harness architecture; it does not imply that Relay can set those options in a
Codex CLI guest. The OpenCode V2 documentation is not evidence for a V1 install.
Claude Code's estimates for API billing are not subscription invoices.

## Relay today: retain these strengths

- Native agents already track provider usage and estimate missing counts, compact
  at `min(80% of window, window - max_output - 24k)`, trim older tool results,
  and preserve recent turns plus deterministic request/todo state. See
  [`context.py`](../backend/relay_core/context.py),
  [`agent.py`](../backend/relay_core/agent.py) and protocol section 4 in
  [`AGENT-SESSIONS-PROTOCOL.md`](AGENT-SESSIONS-PROTOCOL.md).
- Relay already has a `short` prompt profile and deferred groups of tool schemas.
  The protocol records a measured drop from 8,837 to 3,846 tokens on one local
  model with a Board attached. See protocol sections 12.12–12.13 and
  [`prompt_profiles.py`](../backend/relay_core/prompt_profiles.py).
- Guest Codex/Claude harnesses maintain their **own** model context. Relay keeps a
  transcript for display and model handoff, and intentionally skips its own
  automatic compaction when no separate summaries role is configured. The recent
  #CP3M fix also avoids mistaking a guest's turn-aggregate usage for one prompt's
  context size. See [`agent.py`](../backend/relay_core/agent.py) and
  [`guest_harness_provider.py`](../backend/relay_core/guest_harness_provider.py).

## Recommendations, in implementation order

1. **P0 — Show the actual recurring cost.** Add a per-pane and per-turn usage view
   with model, source (native/guest/child), response count, input, cached input,
   output, estimated credits or provider currency when known, and *last prompt*
   size separately from cumulative tokens. Show unknown/missing usage explicitly.
   Aggregate child usage into the task total without double-counting it. Add a
   simple “largest sessions in the last day” view. This follows the observability
   distinction both OpenAI and Claude Code emphasize. Acceptance: the 36-hour
   spike can be attributed from Relay's UI or an export without parsing external
   session JSONL files. The UI must not call cumulative usage “current context.”

2. **P0 — Put an economic trigger beside the context-limit trigger for native
   agents.** Relay's current 80% limit prevents overflow, but a 100k context in a
   258k window can run thousands of steps without compaction. Estimate the
   *marginal repeated-input cost* from recent request sizes, cache fraction,
   model rate and call frequency. Offer a checkpoint/“fresh task context” when the
   projected next N calls cost more than compaction plus rebuilding the cache.
   Keep the existing threshold as the hard safety backstop. Start with a visible
   recommendation and an opt-in setting, not an arbitrary automatic 20k cap.
   Acceptance: a replay of long sessions shows lower cost with no lost open asks,
   decisions, edits or tool-call/result pairs. Measure post-compaction cache misses.

3. **P0 — Bound tool output *before* it enters native model context.** Keep full
   stdout, test logs and search results in a durable artifact; send a short
   structured result with command, exit code, counts, failing excerpts and a
   retrieval handle. Let the agent request a specific range or file when needed.
   Relay currently caps its own command output at 32,768 characters, and its
   compactor later elides old results; both still allow a large result to be
   resent on many steps. Use a smaller default for noisy tools with explicit
   expansion, while preserving full output for the user and for audit. Acceptance:
   a large test/log result costs a bounded number of prompt tokens and remains
   inspectable without rerunning the command. [Claude's filtering example](https://code.claude.com/docs/en/costs).

4. **P1 — Make prompt composition measurable and stable.** Show a `/context`
   style breakdown of instructions, Board policy, tool schemas, messages, tool
   results and attachments. Preserve the byte-identical early prefix across
   turns; append newly loaded tool groups. Keep niche instructions in on-demand
   skills or path-scoped rules. Relay's existing short profile and tool groups
   are the starting point, not work to repeat. Acceptance: a no-op turn changes
   neither the system/tool prefix nor cache eligibility; the breakdown identifies
   exactly which fixed component grew. [OpenAI cache guidance](https://developers.openai.com/api/docs/guides/prompt-caching);
   [Claude context-cost guide](https://code.claude.com/docs/en/features-overview).

5. **P1 — Budget delegation and choose a cheaper default for routine work.** Before
   launching several children, show their models and a rough cost range; warn when
   all inherit the most expensive model. Give focused read-only research and
   verification tasks a lower-cost default, with a clear override. Stop children
   when their bounded assignment ends. The 36-hour audit attributed roughly 14%
   of estimated credits to native subagents, so this matters, but suppressing
   useful delegation wholesale would be the wrong metric. Acceptance: root +
   children appear in one task total, and a multi-agent replay demonstrates
   comparable outcomes at lower cost. [Claude team guidance](https://code.claude.com/docs/en/costs);
   [OpenAI subagent accounting](https://developers.openai.com/api/docs/guides/agents-api/observability).

6. **P1 — Separate guest-harness advice from native controls.** Relay cannot
   compact a Codex/Claude/OpenCode guest's private context by rewriting its
   display transcript. Surface the guest's own reported usage and link or expose
   its own compact/new-session controls when supported. On model handoff, send
   only the context the new guest lacks; Relay's current handoff can include up
   to 160,000 characters of history. Instrument that handoff size and avoid
   repeating it after acknowledgment. Acceptance: guest context readings and
   Relay's transcript estimates are visibly distinct, and a model switch adds
   one bounded handoff rather than repeated bulk history.

## How to decide whether a change helped

Replay a small set of real tasks: a short fix, a long tool-heavy debugging run,
a multi-card verification, a model switch, and a parallel research task. Keep
the same model and task outcome for each A/B comparison. Record total credits or
currency, total and cached input, output, model-call count, peak/median prompt
size, cache rebuilds, elapsed time, and verified task result. Count compaction
calls and children in the total. A saving that loses an open request, forces
expensive rereads, or makes the answer worse is not a win. Pilot the economic
trigger first in diagnostics; use these replays to set defaults by provider.
