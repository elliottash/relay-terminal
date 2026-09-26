---
id: 0C0V
type: work
status: needs-verification
labels: [feature, context, guests, subagents]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 6ce97a62-7d63-4001-9a08-00a24af1ef25
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Claude guest in a Relay pane, 2026-09-23
links: {plans: [], commits: [1caa660baa7b, 419f057dcab1, 4709154bbf94, 2a1a199136a1, 1697e39e869e, 410bd0b90289, 78ab3532b519, 0a60e5846d8f, dc0303aeb78f], evidence: [docs/qa_evidence/2026-09-23-0C0V/], related: [GMCF, C8WX, CP3M, Y63Z, PPR4], github: null}
---
# Token efficiency for every model: usage view, bounded tool output, stable prefix, cheaper subagents, economic compaction

## Issue
carefully analyze docs/TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md and try to make the learnings applicable to all models in relay. clarify anything with me then write your plan to a card

## Plan
Source: `docs/TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md` (9f2055a9). Goal: fewer repeated context tokens per useful outcome, for native agents on every provider, and honest accounting for guests (Codex, Claude Code) whose context Relay does not control.

### Owner decisions (2026-09-23)

1. **Cost figures**: show provider-reported cost where given (OpenRouter, Claude), and also an *estimate* priced at the OpenRouter list price for any model with an OpenRouter listing, always labelled "estimate". No hand-kept subscription credits table.
2. **Size-based compaction**: an Options setting "Compact between turns when the last prompt was over N tokens", **off by default**, N = **256k** by default. No cost advice in the UI (no "this context costs ~$X" lines).
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
8. **Guests.** Record the handover brief size in the turn record and Activity; do not resend it after the guest's first turn acknowledges it (already true, add a test). ⓘ shows guest-reported usage separately from Relay's transcript estimate. 
9. **Size-based compaction (native).** Options › Agent: "Compact between turns when the last prompt was over N tokens", off by default, N = 256,000 by default. Checked after `done`, never mid-turn; `last_prompt` from step 1 (provider usage, else the estimate). Whichever comes first wins: the 80% window trigger stays the hard backstop, so on windows under ~320k the setting only matters if N is lowered. No cost advice in the chip, ⓘ or anywhere else.

### Verification

- Targeted tests per step: `tests/test_agent.py`, `tests/test_system_prompt.py`, `tests/test_sessions.py`, `tests/test_guest_harness_*.py`, a new `tests/test_tool_output_bounds.py`.
- Replays, as the research doc's last section: a short fix, a long tool-heavy debugging run, a multi-card verification, a model switch, parallel research. Same model and outcome, A/B. Record total and cached input, output, calls, peak/median prompt, prefix changes, cost, and the verified result. Evidence in `docs/qa_evidence/2026-09-2x-0C0V/`.
- Acceptance from the research doc: a 36-hour spike attributable from ⓘ/Sessions alone; a large log costs bounded prompt tokens and stays readable without rerunning; a no-op turn and a Board claim leave the prefix unchanged; root + children in one total; a model switch adds one bounded handover.

### Risks

- Clearing tool results and moving the claims line both cost one cache rebuild each time they fire. Thresholds must make that rare; the replay measures it.
- A model may re-read truncated output repeatedly. The replay counts `command_output` range calls.
- Flash subagents may do worse on "read-only" work that is actually hard; the delegating agent can pass `main`, and the replay compares outcomes.
- Pane.h / ⓘ are contested files; land each step separately through `scripts/land.py`.

## Done means
- ⓘ shows reported and estimated usage per turn, total task usage including child agents, and guest handover size without calling cumulative usage “context.”
- `/usage` opens ⓘ; `/context` displays an estimated component breakdown; Sessions can sort by tokens and filter to the last 24 hours.
- Options exposes stale tool-result clearing (on), size-based compaction (off, 256k), and the backend retains prefix-change counts in saved turn records.
- The targeted Python and Qt tests pass, and a live isolated-profile capture shows the UI.

## Tests
- `tests/test_usage_records.py`
- `tests/test_conv_index.py`
- `tests/test_compact_over_tokens.py`
- `tests/test_tool_output_bounds.py`
- `tests/test_subagents.py`
- `tests/test_system_prompt.py::StabilityTests`
- `tests/test_system_prompt.py::PrefixCheckTests`
- `tests/test_system_prompt.py::ContextBreakdownTests`
- `ctest -R conversations`
- manual: docs/qa_evidence/2026-09-23-0C0V/

Direct `python3 -m unittest` invocations passed 206 backend and 22 prompt tests. Five targeted Qt methods passed under Xvfb. `scripts/relay-build --target relay` and `scripts/relay-build --target relay-conversations-tests` passed; `land.py` built the exact merged tree at `78ab3532`. The full Qt `conversations` target has two unrelated UI failures in the shared checkout; a broader `tests.test_system_prompt` run had one existing prompt-size budget failure (9,800 vs 9,728 bytes). Shell runs and screenshots are recorded in the evidence README; the Board's test history has not recorded those shell runs for this revision.

### Check 2026-09-23 15:40
- not-applicable · unittest:tests.test_usage_records — tests/test_usage_records.py is not in the project any more
- missing-evidence · unittest:tests.test_conv_index — no run of tests/test_conv_index.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_compact_over_tokens — no run of tests/test_compact_over_tokens.py for this revision, from any host, and no attached result
- not-applicable · unittest:tests.test_tool_output_bounds — tests/test_tool_output_bounds.py is not in the project any more
- missing-evidence · unittest:tests.test_subagents — no run of tests/test_subagents.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_system_prompt.StabilityTests — no run of tests/test_system_prompt.py::StabilityTests for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_system_prompt.PrefixCheckTests — no run of tests/test_system_prompt.py::PrefixCheckTests for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_system_prompt.ContextBreakdownTests — no run of tests/test_system_prompt.py::ContextBreakdownTests for this revision, from any host, and no attached result
- missing-evidence · ctest:conversations — no run of ctest -R conversations for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-23-0C0V/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-0C0V/
- notice · unittest:tests.test_usage_records — tests/test_usage_records.py is not in the project any more
- notice · unittest:tests.test_conv_index — tests/test_conv_index.py: 125 of 128 never ran here (test_fts_query_escapes_operators_and_prefixes_words, test_query_terms, test_match_line_picks_the_line_and_merges_ranges…)
- notice · unittest:tests.test_compact_over_tokens — tests/test_compact_over_tokens.py: 10 of 11 never ran here (test_fires_after_done_with_its_reason, test_never_mid_turn, test_under_the_bound_does_nothing…)
- notice · unittest:tests.test_tool_output_bounds — tests/test_tool_output_bounds.py is not in the project any more
- notice · unittest:tests.test_subagents — tests/test_subagents.py: 37 of 37 never ran here (test_parallel_foreground_calls_run_concurrently, test_no_nesting_and_tool_restriction, test_plan_mode_delegates_like_build_mode…)
- notice · unittest:tests.test_system_prompt.StabilityTests — tests/test_system_prompt.py::StabilityTests: 6 of 13 never ran here (test_attaching_a_board_changes_nothing_above_the_workspace_line, test_a_claim_mid_conversation_is_a_note_and_the_prefix_does_not_move, test_a_memory_change_mid_conversation_is_a_note_and_the_prefix_does_not_move…)
- notice · unittest:tests.test_system_prompt.StabilityTests — tests/test_system_prompt.py::StabilityTests: 1 of 13 are not in the project any more (test_attaching_a_switchboard_changes_nothing_above_the_workspace_line)
- notice · unittest:tests.test_system_prompt.PrefixCheckTests — tests/test_system_prompt.py::PrefixCheckTests: 6 of 6 never ran here (test_an_edited_earlier_message_is_an_unexpected_change, test_a_change_the_code_meant_is_expected_with_its_reason, test_detaching_the_board_rebuilds_the_prompt_and_says_which_sections…)
- notice · unittest:tests.test_system_prompt.ContextBreakdownTests — tests/test_system_prompt.py::ContextBreakdownTests: 3 of 4 never ran here (test_a_loaded_group_is_its_own_part, test_an_image_counts_as_an_image_and_a_guest_pane_says_so, test_the_request_answers_with_the_breakdown)
- notice · ctest:conversations — ctest -R conversations is slow: p95 3.18 s, p50 2.91 s
history: thread

## Execution Summary
Backend parts A–D landed in five commits: saved per-turn and child usage, list-price estimates, guest handover accounting, bounded and rereadable tool results, stale-result clearing, stable prompt prefixes, context breakdowns, cheaper subagent defaults, and opt-in 256k size-based compaction. The GUI work adds ⓘ turn and task totals, `/usage`, `/context`, a Sessions Tokens column with sort and Last 24 hours filter, and Agent Options controls. The saved turn record now retains `prefix_changes`; provider cost and OpenRouter list-price estimate can appear side by side. The implementation commits are linked above.

![ⓘ usage by turn and task total](docs/qa_evidence/2026-09-23-0C0V/usage-info.png)

![Sessions token column and sort](docs/qa_evidence/2026-09-23-0C0V/sessions-tokens.png)

These images use staged worker events in real Qt widgets under Xvfb and an isolated profile. Validation commands and known unrelated test failures are in the evidence README. Paid-model A/B task replays remain for independent verification before any default pruning or compaction threshold is tightened.
