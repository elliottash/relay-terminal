<!-- relay:entry 20260921T021051Z-e8 author=agent kind=event model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 -->
- ✦ agent created this card in Planned · issues/changes/2026-09-20-away-recaps-repeat-indefinitely-pane-and-worker.md

<!-- relay:entry 20260921T021103Z-vg author=agent kind=event model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3-flashx, session b96540de

<!-- relay:entry 20260921T021103Z-vh author=agent kind=progress model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 pane_token=b96540de-f8ec-49a0-b157-14b6ae18adee -->
Claimed (b96540de) · working on it from a terminal pane

Root cause found in logs: pane e273439f got five reason=away recaps in a row (01:49–01:57) because the pane counts only outcome=done turns while the worker counts errored/killed turns too, so m_turnsCompleted (3) never equals turns_covered (4). Fixing worker-side with a persisted last-recap turn marker.

<!-- relay:entry 20260921T022650Z-11 author=agent kind=event model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260921T022704Z-1k author=agent kind=event model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260921T022721Z-kh author=agent kind=event model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 -->
- ✦ agent moved this card · Executing → Done, session b96540de released · Worker-side dedupe landed: Agent.recap_turn persisted per session; away/resume recaps over already-covered turns answer skipped no_new_turns. Covered by test_away_recap_not_repeated plus the full test_session_protocol (35) and test_sessions (52) suites. · verified_by glm/glm-5.3-flashx

<!-- relay:entry 20260921T023024Z-01 author=agent kind=evidence model=z-ai/glm-5.3-flashx pane=b96540de turn=d5348efc30e1448e94371184d35b4adc/c4ba451554ab499ead651837ce8c63d4 -->
Landed as c8f7bce2c9c333800b049746cee32e481f72eb31 (backend/relay_core/agent.py, session_protocol.py, tests/test_session_protocol.py, docs/AGENT-SESSIONS-PROTOCOL.md, this card). Verified: `PYTHONPATH=backend python3 -m unittest tests.test_session_protocol` 35/35 OK, `tests.test_sessions` 52/52 OK. The other session's uncommitted "Helper agent" hunk in docs/AGENT-SESSIONS-PROTOCOL.md was left in the working tree, not committed.
