---
id: 2CZP
type: work
status: ready
labels: [feature, agent]
rank: zzzzzzzzzzzy
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Uncap turn limits by default; loop detection, recitation reminders, LLM double-check

## Issue
i would uncap both. can you research, i think there is work /advice that says, if an agent is doing a long chain of thought, long tool calls, or long turns, you could give it regular reminders that could break bad / recursive looping.

yes, file as a card. and i think i really want it uncapped by default. people want to have long agent runs over night now in the age of astra.

i agree with all 3 layers you proposed

## Decisions
- Uncap `agent/max_steps` and `agent/max_tool_calls` **by default** (overnight-long agent runs; owner, 2026-09-20). Keep the settings as a configurable backstop fuse at the clamp maxima (500 steps / 2000 tool calls), not as the working default.
- Replace the hard-stop-at-limit UX with three layers (owner agreed to all three, 2026-09-20):
  1. **Deterministic loop detection**, checked every step, zero model cost. Track `(tool, normalized-args-hash, result-hash)`; thresholds from OpenHands/Gemini CLI: same call + same result 4×, same error 3×, alternating ping-pong cycle 6×, monologue 3× (model-role messages only). On trigger, inject a short user-role message naming the pattern ("last 4 calls were X with the same error — change approach or report blocked"). After 2 ignored nudges, end the turn cleanly via the existing `stop_reason: limit` + Continue path.
  2. **Cadence recitation** every ~25 model steps or 50 tool calls, whichever first: compact reminder with the original request, open ledger items, todos, one line of recent progress (Manus todo.md recitation pattern; the ledger/todos already hold this state). Silent when nothing is open.
  3. **LLM double-check on detector trigger only** (not on a timer): a Lite-role model with a Gemini-style diagnostic prompt, including the productive-repetition whitelist (batch ops across files, incremental same-file edits, retry with variation, re-running builds after edits are NOT loops).
- Every layer stays silent when nothing is wrong (same rule as the existing 8-step stale-todo nudge, `docs/ARCHITECTURE.md:1825`).
- Not adopted: every-8192-thinking-token reminders (can't inject mid-generation; step/tool cadence covers it); timer-based LLM checks (cost, mostly say "fine").

## Tasks

- [ ] Raise default max_steps/max_tool_calls to clamp maxima (or treat as backstop only); keep Options > Security rows and protocol 12.1 semantics for explicit values <!-- t:6v -->
- [ ] Worker-side deterministic loop detection over (tool, args-hash, result-hash) with OpenHands/Gemini thresholds; nudge injection, 2 ignored nudges -> clean limit stop <!-- t:w7 -->
- [ ] Cadence recitation reminder (~25 steps / 50 tool calls) rendered from request ledger + todos <!-- t:tx -->
- [ ] LLM double-check on detector trigger via Lite role with productive-repetition whitelist prompt <!-- t:z9 -->
- [ ] Update docs/AGENT-SESSIONS-PROTOCOL.md section 12 (limits + new reminder events) and docs/ARCHITECTURE.md <!-- t:e4 -->
- [ ] Tests: detector unit tests (loop vs. productive batch ops), cadence reminder timing, nudge-escalation path <!-- t:k8 -->
