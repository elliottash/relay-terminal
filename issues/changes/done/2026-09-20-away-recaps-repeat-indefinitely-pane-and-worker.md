---
id: TKKA
type: work
status: done
labels: [bug, agent]
assignee: agent
implemented_by: glm/glm-5.3-flashx
verified_by: glm/glm-5.3-flashx
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [D54R, MVGR], github: null}
---
# Away recaps repeat indefinitely: pane and worker count turns differently, so the dedupe never trips

## Issue
prevent two recaps in a row -- you can see that here: 28867cb1c4f8449f9a03dffa0ffd27ee

## Execution Summary
Root cause, from `~/.local/share/relay/logs/relay.log` and `worker.log` for session 28867cb1c4f8449f9a03dffa0ffd27ee: pane e273439f printed five `reason=away` recaps in a row (01:49:38, 01:51:04, 01:56:36, 01:56:52, 01:57:33 on 2026-09-21), all covering the same span 18:52 → 21:46, and pane 430f45cf got pairs ~40 s apart. The pane's dedupe (`m_turnsCompleted == m_lastRecapTurns`, src/Pane.h `awayRecapAllowed`) compares the pane's count of turns that ended `done` (3) against the worker's `turns_covered` (4); the worker also counts the errored turn at 00:45:34 and the turn killed by the 22:54 gdb restart, so the two numbers never agree and the guard never trips again.

Fix, worker-side and authoritative: `Agent.recap_turn` records the turn count the last written recap covered (persisted in the session file's `recap_turn`, reset with the conversation via `_new_session`). `SessionCommands._start_recap` answers an `away` or `resume` request over the same turns with `recap {skipped: "no_new_turns"}` instead of writing a duplicate; `manual` always runs. The pane's guard stays as a fast path, so a skipped away/resume recap is silent there (status is shown only for manual). Protocol documented in docs/AGENT-SESSIONS-PROTOCOL.md ("One recap per stretch of work").

Files: `backend/relay_core/agent.py`, `backend/relay_core/session_protocol.py`, `tests/test_session_protocol.py`, `docs/AGENT-SESSIONS-PROTOCOL.md`. A src/Pane.h comment was deliberately left out: the shared checkout carries another session's uncommitted hunks there.

## Tests
`tests/test_session_protocol.py`:
- new `test_away_recap_not_repeated` — a second away recap over the same turns is skipped (`no_new_turns`) and costs no model call, a manual recap always runs, a new turn re-arms the away recap, and the marker survives save + resume (a resumed session with no new turns does not recap again).
- `test_compact_resume_recap_and_plan_execute` updated — the away recap right after the resume recap is now the skipped duplicate.

Run: `PYTHONPATH=backend python3 -m unittest tests.test_session_protocol` (35 tests, OK) and `tests.test_sessions` (52 tests, OK).
