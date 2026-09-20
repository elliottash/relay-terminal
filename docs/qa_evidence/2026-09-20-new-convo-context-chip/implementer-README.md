# #5PY9 — new conversation kept the old one's context reading

Implementer evidence, 2026-09-20. Card: `issues/features/2026-09-19-new-convo-had-incorrect-context-remaining.md`.

## What was wrong

`/new` (and delete-current-conversation, and "execute in fresh context") go through the worker's
`reset` → `Queue.reset()` → `Agent.reset_conversation()`. That rebuilt the session and emitted
`requests`/`todos`/`title`/`summary` — but never a `context` event, so the pane's context chip kept
the *previous* conversation's "81% left" until the next turn's first `context` event. Every other
conversation-replacing path (`_load_state`, `_resume`, `_rewind`, `_set_model`, `_context`) already
emits one.

## The change

- `backend/relay_core/agent.py`, `Agent.reset_conversation()`: emits `self.context_event()` inside
  the existing `if self._announce:` block, after title and summary. One emit fixes all three reset
  callers.
- `src/Pane.h`, the `reset` event handler: now also `clearNextContext()`, `m_compacting = false`,
  then `updateContextLabel()` — a pending model switch's "next" window or a compaction flag must not
  survive into the new conversation's chip. The context members themselves are **not** zeroed: the
  fresh `context` event arrives immediately *before* `reset`, and zeroing would flash "100% left".
- `docs/AGENT-SESSIONS-PROTOCOL.md` §4: `reset` is listed among the conversation-replacing paths
  that emit the new conversation's `context`, before the `reset` event.

## Verification

- `tests.txt` (next to this file): the three test files that exercise `reset_conversation` / the
  worker `reset` request — `test_agent`, `test_session_protocol`, `test_sessions` — 101 tests, OK,
  including the two new ones:
  - `test_agent.AgentTests.test_reset_conversation_emits_the_fresh_context` — one `context` event
    after `reset_conversation()`, `used_tokens` below the pre-reset figure, `percent < 10`,
    same window.
  - `test_session_protocol.WorkerSubprocessTests.test_reset_is_preceded_by_the_new_conversations_context`
    — end to end against a live `worker.py` subprocess and a loopback model server: a real turn,
    then `reset`; the emitted sequence carries the fresh `context` between `agent_finished` and
    `reset`, its `used_tokens`/`percent` are the fresh conversation's, and `reset.session_id`
    differs from the first session's.
- `scripts/relay-build`: green (42 s incremental, 2026-09-20.00H.02 build).
- Full `ctest --test-dir build -j4` ran once before the owner asked for limited runs: 63/64 passed;
  `backend-and-bash` (the whole python discovery) had 9 subtest failures **in files this change does
  not touch** (the shared checkout carries several other sessions' uncommitted work at the moment).
  The files this change can reach all pass (see `tests.txt`). Not re-run per the owner's
  instruction; the 9 failures were not identified further and are left to the sessions that own
  those files.

## By hand (owner / QA, from the card's plan)

Run a conversation until the chip is visibly down, press `/new`: the chip should go straight to
~97% left (system prompt + workspace instructions + tool schemas are real context; 99–100% is not
reachable), tooltip "3,5xx of 128,000 tokens (estimated)". No flash of "100% left" in between.
