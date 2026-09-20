---
id: 5PY9
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
priority: 2
rank: zzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [ca3c8d61], evidence: [docs/qa_evidence/2026-09-20-new-convo-context-chip/], related: [], github: null}
---
# new convo had incorrect context remaining

## Issue
i did a /new convo and it says 81% left in the context button. that cant be write, should be 99/100?

## Plan

### Goal
After `/new` (and every other path that starts a fresh conversation) the context chip must show the
new conversation's usage, not the one it had a moment before. A fresh conversation in this
workspace measures ~3.6k of 128k tokens, so the chip should read **97% left**, not 81%.

### Findings
- The chip is driven entirely by the worker's `context` event:
  `src/Pane.h:5709` stores `used_tokens` / `window` / `limit_tokens` / `percent` / `estimated` into
  `m_ctxUsed` … `m_ctxEstimated`, and `Pane::updateContextLabel()` (`src/Pane.h:3557`) renders
  `"%1% left"` from `100 - percent`. Nothing else ever changes those members, so between two
  `context` events the chip keeps whatever it last heard.
- `/new` sends `{"type":"reset"}` (`Pane::newChat()`, `src/Pane.h:715`). The pane's `reset` handler
  (`src/Pane.h:5988`) resets `m_turnsCompleted` and `m_sessionId` and **does not touch the context
  members**.
- The worker's `reset` branch (`backend/worker.py:347`) calls `turns.reset()` →
  `Queue.reset()` (`backend/relay_core/queue.py:81`) → `Agent.reset_conversation()`
  (`backend/relay_core/agent.py:455`). That rebuilds the session (`_new_session()` sets
  `self.messages = [system]` and calls `context_invalidate()`) and emits `requests`, `todos`,
  `title` and `summary` — **but never `context_event()`**. The worker then emits `reset` and stops.
- So the backend's own numbers are correct after the reset; they are simply never sent. The chip
  goes on showing the previous conversation's percentage until the next turn's first
  `context` event (`agent.py:1105` / `1299`), which is when the owner's 81% would finally drop.
- Every other conversation-replacing path already emits it, which is what makes this one the
  outlier: `_load_state` (`session_protocol.py:370`), `_resume` (`:393`), `_rewind` (`:352`),
  `_set_model` (`:314`), `_context` (`:322`), compaction (`agent.py:1105`).
- Two more callers hit the same gap through `turns.reset()`: deleting the conversation the pane is
  showing (`session_protocol.py:994-997`) and "execute in fresh context"
  (`_plan_execute`, `session_protocol.py:1208-1212`).
- Measured on this checkout with the `tests/test_agent.py` harness
  (`Agent(CONFIG, "/home/elliott/repos/relay-terminal", emit, provider=Fake)`): a brand-new
  conversation is `used_tokens: 3567, window: 128000, percent: 2.8` — i.e. **97% left**. The owner's
  "99/100" is not reachable: the system prompt, the workspace instructions and the tool schemas are
  real context, and the tooltip already says so ("3,567 of 128,000 tokens (estimated)"). 81% is
  three conversations' worth of history, not a rounding difference.

### Steps
1. In `backend/relay_core/agent.py`, `Agent.reset_conversation()`: inside the existing
   `if self._announce:` block, emit `self.context_event()` alongside the title and summary events.
   One line, and it fixes all three reset callers at once (`/new`, delete-current-conversation,
   execute-in-fresh-context) rather than patching `worker.py` only.
2. In `src/Pane.h`, the `reset` event handler (`~:5988`): also clear the pending-switch context
   (`clearNextContext()`) and `m_compacting = false`, then `updateContextLabel()`. A reset while a
   model switch was still waiting otherwise leaves the chip on the old conversation's `↻` and the
   stale "next model" window. Do **not** zero `m_ctxUsed`/`m_ctxPercent` here — the backend's
   `context` event arrives immediately before the `reset` event and already carries the truth;
   zeroing would make the chip flash "100% left".
3. `docs/AGENT-SESSIONS-PROTOCOL.md`: in the reset/new-conversation wording, say that `reset` is
   followed by `context`, the same as `load_state`, `resume` and `rewind` are.

### Risks
- Ordering: `Agent.reset_conversation()` emits *before* `worker.py` emits `{"event":"reset"}`, so
  the pane applies the new numbers and then handles `reset`. Step 2 must not undo them — that is why
  it clears only the pending-switch state.
- A pane with no window yet (`m_ctxWindow <= 0`) hides the chip; nothing here changes that.
- Subagent and guest panes go through the same `Agent`, so they gain the event too. Harmless, but
  worth a glance at `tests/test_session_protocol.py` for any test asserting the exact event sequence
  after `reset`.
- **For the owner:** the chip will read ~97% left on a fresh conversation, not 99-100%. The missing
  3% is the system prompt, the workspace instructions and the tool schemas, which genuinely occupy
  the window. Is that the wanted reading, or should a fresh conversation's baseline be shown as
  "full" with the chip measuring only what the conversation adds to it? The former is what Claude
  Code and Codex show, and is the assumption this plan builds on.

### Verify
- New test in `tests/test_agent.py`: build an `Agent` with the `FakeProvider` harness, run one
  `ask`, assert the last `context` event's `percent` is high, then call `reset_conversation()` and
  assert **a `context` event was emitted after the reset** and that its `used_tokens` is back to the
  fresh-conversation figure (a few thousand, well under 10% of the window).
- New test in `tests/test_session_protocol.py` covering the `reset` request end to end: the emitted
  sequence contains `context` and then `reset`.
- `python3 -m pytest tests/test_agent.py tests/test_session_protocol.py`, then
  `scripts/relay-build` and `ctest`.
- By hand: run a conversation until the chip is visibly down (a few big file reads will do), press
  `/new`, and check the chip goes straight to "97% left" with a tooltip reading
  "3,5xx of 128,000 tokens (estimated)". Screenshot before and after into
  `docs/qa_evidence/` for the card.

## QA checklist
- [ ] By hand (the plan's live check): run a conversation until the context chip is visibly down, press `/new` — the chip goes straight to ~97% left (system prompt + workspace instructions + tool schemas are real context), never the old conversation's figure, and does not flash "100% left" on the way. Tooltip: "3,5xx of 128,000 tokens (estimated)".
- [ ] `/new` while a model switch is still pending, or mid-compaction: the chip loses the old conversation's ↻ / "compacting…" state with it.
- [ ] Unit tests green: `tests/test_agent.py::AgentTests::test_reset_conversation_emits_the_fresh_context`, `tests/test_session_protocol.py::WorkerSubprocessTests::test_reset_is_preceded_by_the_new_conversations_context` (both in `docs/qa_evidence/2026-09-20-new-convo-context-chip/tests.txt`, 101 tests OK).
- [ ] Fresh-conversation reading is ~97% left, not 99–100% (owner call noted in the plan's Risks; assumed as wanted).
