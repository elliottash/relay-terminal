# #VQXA — todo reminders and recitals gated on something to say

Implementer evidence, 2026-09-25 (anthropic/claude-opus-5-5 via claude-code).

## Change
- `backend/relay_core/todos.py`: `update_reminder_needed(open_todos)` — true only when an open item is `pending`.
- `backend/relay_core/agent.py`: the 8-step staleness note fires only when that is true; the cadence recital
  (25 steps / 50 calls) is said only once a warrant has been set since the last one — a steer, a model
  takeover, a mid-turn compaction (message count shrank) or a tool call ≥ `LONG_TOOL_WAIT_S` (30 s).
  Every injected reminder logs `reminder_injected kind={todos_stale,todos_missing,recitation,completion} step=… tool_calls=…`.
- The no-list nudge and completion check are unchanged apart from the log line.

## Tests
- `targeted-tests.txt`: the reminder/recital cases in `tests/test_requests.py` and `tests/test_agent.py`.
- Replay of the #234Z shape: `test_single_in_progress_todo_gets_no_stale_reminder` — one `in_progress` todo,
  40 tool steps, no event: zero staleness notes, zero recitals, no `reminder_injected` log line.
- `test_recital_waits_for_an_event_after_the_cadence`, `test_a_mid_turn_compaction_warrants_the_recital` (a no-op
  compaction warrants nothing), `test_a_slow_tool_call_warrants_the_recital`, and in `tests/test_agent.py`
  `test_the_cadence_alone_recites_nothing` (60 quick steps, zero recitals).
- `python3 -m pytest tests/test_requests.py tests/test_agent.py tests/test_todo_subagents.py -q` → 110 passed,
  also on a clean `git archive` export of the landed tree.
