---
id: 0C0V
type: work
status: discussing
labels: [feature, context, guests, subagents]
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Claude guest in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [GMCF, C8WX, CP3M, Y63Z, PPR4], github: null}
---
# Token efficiency for every model: usage view, bounded tool output, stable prefix, cheaper subagents, economic compaction

## Issue
carefully analyze docs/TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md and try to make the learnings applicable to all models in relay. clarify anything with me then write your plan to a card

## Plan
Source: `docs/TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md` (9f2055a9). Goal: fewer repeated context tokens per useful outcome, for native agents on every provider, and honest accounting for guests (Codex, Claude Code) whose context Relay does not control.

### Owner decisions (2026-09-23)

1. **Cost figures**: show provider-reported cost where given (OpenRouter, Claude), and also an *estimate* priced at the OpenRouter list price for any model with an OpenRouter listing, always labelled "estimate". No hand-kept subscription credits table.
2. **Economic compaction trigger**: OPEN, see Questions below.
3. **Tool output**: a command result enters model context at 12,000 characters (head + tail, exact byte/line counts, a re-read handle); the user's fold and the job buffer keep everything. Stale tool results are batch-cleared between compactions, **on by default**, conservative threshold, keeping the last few tool groups.
4. **Cheaper subagents**: yes. See step 5 for how the default and the delegating agent's choice combine.
5. **Usage view**: extend the ⓘ pane (per-turn table, task total incl. children), `/usage` as its keyboard twin, a tokens sort in the Sessions manager. No new pane.

### What the code does today (findings)

- Per-call `usage` reaches Activity (`src/AgentInternalsView.cpp`, `noteUsage`); session totals incl. `cached_tokens`, `cache_write_tokens`, `cost` reach ⓘ (`src/SessionInfo.cpp`) and the saved session `.meta.json` (`backend/relay_core/sessions.py`, `OPTIONAL_USAGE`). Per-turn usage lives only in memory (last `MAX_TURN_LOG` = 50 turns). Children are not summed into the parent. Local store: 618 sessions with usage; the largest each carry ~50M prompt tokens, mostly guest sessions.
- Only trigger for compaction is the window limit, `min(80% window, window - max_output - 24k)` (`backend/relay_core/context.py`). No price data outside the OpenRouter catalog.
- `tools.MAX_OUTPUT = 32768`: a command result enters context at up to 32 KB and is resent on every later step until compaction elides it. `jobs.py` keeps 1 MiB and 16 finished jobs, but the read tool returns only unread bytes, so there is no range re-read.
- Cache-buster: the Board claims line ("You hold: #X") and the memory section are inside the system message (`agent.py` prompt assembly, `refresh_system_prompt()` callers in `activity_tools.py`, `observe_protocol.py`). A mid-conversation claim rewrites the first message, which invalidates the whole cached prefix on hosted providers. The "last in the prompt" ordering protects only llama.cpp's slot reuse. No prefix-change counter, no `/context` breakdown.
- Subagents with no `model` use the `subagent` role, else Main (`subagents.py` `base()`). The `model` argument already accepts `inherit`, a preset id or a role alias (`flash`, `high`...). Definitions carry a read-only flag. Finished children report `tokens`, not joined to the parent total.
- Guests: context shown separately (#C8WX, #CP3M), `/compact` asks the guest to compact itself (`thread/compact/start` for Codex, `/compact` for Claude), handover brief (`planning.render_transcript`, up to 160,000 chars) sent once per fresh harness, size not recorded.

### Steps

1. **Usage records (backend).** Per turn, store `{model, source: native|guest|child, requests, prompt, cached, cache_write, completion, cost, cost_estimate, last_prompt}` in the session file (bounded list, oldest folded into totals). `last_prompt` is the latest single request's prompt size, never a cumulative figure. Missing counters stay missing ("not reported"), never zero. Children: add a `children` total to the parent session and a task total = own + children, each child counted once (keyed by child session id). Price estimate from the OpenRouter catalog's per-million prices incl. cache-read price when listed. Protocol §4 and §25 updated.
2. **Usage view (GUI).** ⓘ gets a per-turn table and a task total row; `/usage` opens it; Sessions manager gets a Tokens column and sort (from `conv_index`, which already has `tokens`/`cost`), with a "last 24 h" filter, which is the largest-sessions view. The context chip keeps saying context; cumulative usage is never labelled context.
3. **Bounded tool results (native).** Command results enter the model at 12,000 chars: head + tail, `[N lines / M bytes omitted: command_output(job, from, to) to read]`, exit code. `command_output` gains `from`/`to` (line or byte range) over the kept buffer; finished-job retention rises so a handle from this conversation stays readable (bounded by bytes, not count). Same bound for `read_file` without a range and for search results. User-visible fold unchanged.
4. **Clearing stale tool results (native).** Between compactions, when tool-result bytes older than the last K tool groups exceed a threshold, replace them in one batch with a one-line stub (tool, args, size, handle). One batch = one cache rebuild, so it fires rarely. On by default; option in Options › Agent. Never splits a tool call from its result. Uses `context.trim_tool_outputs` logic, moved earlier.
5. **Cheaper subagents.** The delegating agent decides per task: the delegate tool's `model` description tells it to pass `flash` for search, reading, summarising and checking, `main` for implementation, `high` for hard reasoning. When it names none, a read-only definition defaults to the Flash role and any other to Main; the user's `subagent` role setting, when set, overrides both. Before a batch of ≥3 children, the parent's transcript shows their models; a warning when all inherit the High tier.
6. **Stable prefix.** Move the Board claims line and the memory section out of the system message into a context note appended at the turn they change (`[Relay context: ...]`, as terminal context already is), so the system message and tool list are byte-identical across a session. Record a prefix hash per request; Activity marks a request whose prefix changed and says which part (system, tools, earlier messages). Extend `tests/test_system_prompt.py` StabilityTests: a claim and a memory change leave the prefix unchanged.
7. **`/context` breakdown.** Tokens by part: system prompt, Board policy, memory, tool schemas (per loaded group), messages, tool results, attachments. Estimated (4 chars/token) unless the provider can count; says so.
8. **Guests.** Record the handover brief size in the turn record and Activity; do not resend it after the guest's first turn acknowledges it (already true, add a test). ⓘ shows guest-reported usage separately from Relay's transcript estimate. The economic advice (step 9) points guests at their own `/compact` and new-session controls.
9. **Economic compaction.** Per model, marginal repeated-input cost of the next N calls = last_prompt × cached rate (or input rate on miss) × N, from the step-1 records. When it exceeds the one-off cost of compacting plus rebuilding the cache, the context chip tooltip and ⓘ say so with the figures. The automatic part waits on the open question below. The 80% window trigger stays the hard backstop.

### Verification

- Targeted tests per step: `tests/test_agent.py`, `tests/test_system_prompt.py`, `tests/test_sessions.py`, `tests/test_guest_harness_*.py`, a new `tests/test_tool_output_bounds.py`.
- Replays, as the research doc's last section: a short fix, a long tool-heavy debugging run, a multi-card verification, a model switch, parallel research. Same model and outcome, A/B. Record total and cached input, output, calls, peak/median prompt, prefix changes, cost, and the verified result. Evidence in `docs/qa_evidence/2026-09-2x-0C0V/`.
- Acceptance from the research doc: a 36-hour spike attributable from ⓘ/Sessions alone; a large log costs bounded prompt tokens and stays readable without rerunning; a no-op turn and a Board claim leave the prefix unchanged; root + children in one total; a model switch adds one bounded handover.

### Risks

- Clearing tool results and moving the claims line both cost one cache rebuild each time they fire. Thresholds must make that rare; the replay measures it.
- A model may re-read truncated output repeatedly. The replay counts `command_output` range calls.
- Flash subagents may do worse on "read-only" work that is actually hard; the delegating agent can pass `main`, and the replay compares outcomes.
- Pane.h / ⓘ are contested files; land each step separately through `scripts/land.py`.

### Question for the owner (blocks step 9's automatic part only)

When a long conversation has a big context (say 100k tokens) that is well below the window, Relay never compacts it, so every later step re-sends those 100k tokens. Cached, they are cheaper, but still paid on every call. Compacting shrinks it to maybe 15k, but costs one summary call and loses detail.

- **(a)** Add an Options setting, off by default: "Compact between turns when the last prompt was over N tokens" (N e.g. 60k). Plus the advice figures.
- **(b)** Only the advice now ("this context costs ~$X per 100 more steps; /compact would save ~$Y"), and decide on automatic once the replays show the trade-off.

Recommendation: (a), off by default, so it is available for the replay without changing anyone's behaviour.
