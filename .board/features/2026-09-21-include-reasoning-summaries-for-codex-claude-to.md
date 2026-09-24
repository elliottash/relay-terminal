---
id: 9EZJ
type: work
status: planned
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# include reasoning summaries for codex / claude to fill the reasoning bubbles

## Issue
include reasoning summaries for codex / claude to fill the reasoning bubbles

## Done means
A pane running on `guest:codex` or `guest:claude` shows the guest's reasoning streaming into the reasoning fold ("✦ thinking… / thought for N s") during a turn, the way Relay's own providers already do. Failure is recognised by today's symptom: the fold stays empty — or never appears — on a guest turn that plainly reasoned, while the same pane on a Relay provider fills it. A change that only fixes one of the two guests is half the card, not done.

## Plan
**Goal.** Make the codex and claude guest harnesses actually *receive* reasoning from their CLIs, so the already-built thinking pipeline fills the pane's reasoning fold for guest turns.

**Findings.**

- The downstream pipeline exists and is tested, end to end. `guest_harness_codex.py` handles `item/reasoning/textDelta` and `item/reasoning/summaryTextDelta` (`_on_item_reasoning_textDelta`, ~line 628) and `guest_harness_claude.py` handles `thinking_delta` (`_on_stream_event`, ~line 722); both emit `HarnessEvent("thinking")`. `HarnessProvider._Turn._on_thinking` (`guest_harness_provider.py:946`) turns those into `thinking_delta` / `thinking_done` events, which is exactly what fills the fold for Relay's own providers (tests: `test_guest_harness_provider.py:339`, `test_guest_harness_claude.py:688`).
- So the gap is upstream: the guests are never *asked* to produce reasoning.
  - **Codex:** `CodexHarness._start_thread` (`guest_harness_codex.py`) puts only `model_reasoning_effort` in `thread/start`'s `config` map. Codex sends no `item/reasoning/*` notifications unless the thread config enables summaries — the `model_reasoning_summary` key family (`auto|concise|detailed|none`), same `-c` mechanism as the effort. The recorded fixtures (`tests/fixtures/guest_harness_codex/*.jsonl`) contain zero reasoning notifications, consistent with the empty bubbles the owner sees.
  - **Claude:** `ClaudeHarness._argv` never passes `--settings`, and `start_provider` constructs `ClaudeHarness()` with none. The CLI only emits `thinking` blocks when thinking is enabled (a settings key — the lead is `alwaysThinkingEnabled`, with `MAX_THINKING_TOKENS` as the fallback mechanism). `guest_launch.py`'s docstring records that `--settings` takes a file **or inline JSON**, so no settings file needs writing.
- The Tier B TUI launch path (`guest_launch.codex_argv` / `claude_argv`) is out of scope: the reasoning fold is fed by harness events, and a TUI guest drives its own display.

**Steps.**

1. **Reproduce and record.** On this machine (both CLIs installed and signed in), drive one real turn per guest harness and capture the wire: confirm no `item/reasoning/*` / `thinking_delta` arrives today. Save the captures under `docs/qa_evidence/<date>-guest-reasoning-summaries/` as the "before".
2. **Codex:** read `codex app-server generate-json-schema --out <dir>` for the exact summary key and its values, then add it (expected: `"model_reasoning_summary": "auto"`) to the `config` map in `_start_thread`, so start, resume and fork all get it. Note it in the module docstring beside the effort key.
3. **Claude:** verify against the installed CLI (a one-turn probe) which mechanism makes `-p --include-partial-messages` stream `thinking_delta`; then add it to `_argv` — expected `--settings '{"alwaysThinkingEnabled": true}'` inline JSON, with an explicitly passed `_settings` still winning.
4. **Tests:** extend the codex side — `item/reasoning/summaryTextDelta` lines in a fixture (or a new one) asserting `thinking` events, and update the `config` assertion at `tests/test_guest_harness_codex.py:442`. Add a claude test asserting the settings flag is on the argv. Keep the existing thinking-flow tests green.
5. **Docs:** note the summary/thinking enablement in `docs/AGENT-SESSIONS-PROTOCOL.md` §29 (the launch-config facts both harness docstrings record).
6. **Verify live** (step for the same evidence dir): one real turn per guest in a pane, showing the fold fill with summary text and close with "thought for N s".

**Risks.**

- The exact key names (`model_reasoning_summary`, `alwaysThinkingEnabled`) are leads, not facts: they must be read off the installed CLIs (schema dump / live probe), not guessed. If a probe shows a different mechanism, use that and record it.
- The card asks for *summaries*, matching codex's term: do **not** enable raw reasoning (`show_raw_agent_reasoning` / `item/reasoning/textDelta` is handled but its arrival is not the goal).
- If the installed claude simply will not stream thinking content to a `-p` host (redaction), that is a finding to report back on the card — not something to fake from other fields.
- Summaries add tokens/latency to each guest turn; expected to be small, and the owner asked for them.

**Verify.**

- `python3 -m pytest tests/test_guest_harness_codex.py tests/test_guest_harness_claude.py tests/test_guest_harness_provider.py -x` (and `test_guest_launch.py` if launch code is touched).
- Live: a pane on `guest:codex` and one on `guest:claude`, one turn each — the reasoning fold streams and closes with elapsed seconds; captures in the evidence directory named in step 1.
