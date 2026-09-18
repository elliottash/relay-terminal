---
id: 1T0W
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (GUI E1 subagent), 2026-09-17
rank: x3
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`; contract `docs/AGENT-SESSIONS-PROTOCOL.md`
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Steering: add a prompt to the running agent turn at its next tool call

## Behavior as implemented

- **Backend** (`backend/relay_core/queue.py`, `agent.py`, `worker.py`): `ask {when:"steer"}` while a turn runs holds the prompt; the agent loop takes held prompts at the next step boundary (after tool results, before the next model call) and appends them as a user message; `steer_delivered {ids, request_ids}` is emitted. Prompts left when the turn ends emit `steer_returned {id, request_id, prompt, requeued}` and are requeued at the front unless the request had `requeue:false`. When no turn runs, steer behaves like queue. `queue_steer {item}` upgrades a queued item. `queue_changed` lists `steering`. `queue_unsteer {request, as_request}` escalates a steer the turn has not taken yet: it leaves the hold, stops the running turn and is resubmitted as `when:"interrupt"` under `as_request`, keeping its ledger entry; the reply is `steer_escalated {request_id, new_request_id, escalated, ledger_id}`, and `escalated:false` once the prompt was delivered or returned (nothing is stopped). A failed resubmit puts the prompt back at the head of the queue. Tests: `tests/test_queue.py` SteerTests (7).
- **GUI:** an agent prompt entered with Enter while the agent works is queued as before (status: "Enter again to send at the next tool call"). Pressing Enter on the empty prompt box within 15 s upgrades the last queued agent prompt: it leaves the queue, is sent as `ask {when:"steer", requeue:false}`, and shows in the strip as "↪ next tool call ✦ …" (purple). On delivery it prints "› prompt ↪ at the next tool call" in the turn. If the turn ends first, Relay puts the prompt back at the head of its own queue and runs it next. A third Enter within 15 s escalates: the turn is stopped and the prompt runs as its own turn ("Interrupting the current turn; completed actions are not rolled back."). If the agent already took the prompt, nothing is interrupted and a toast says so ("The agent already has it · nothing was interrupted").

## Implementer check (not a QA verdict)

Under Xvfb with Kimi K3 (`docs/qa_evidence/2026-09-17-steering/`): during "run sleep 6; echo one, then sleep 6; echo two, then summarize", "please also include the current year in your summary" + Enter, Enter showed the steering row (`implementer-strip-next-tool-call-badge.png`); it was delivered after the second command, the agent ran `date +%Y` and included 2026 in the summary (`implementer-steer-delivered-mid-turn.png`).

Not verified live: the returned-to-queue path (covered by backend tests).

## QA checklist

1. Start a multi-step task; queue a correction; Enter again; the agent changes course after its current tool call.
2. Steer during the final answer (no more tool calls): the prompt runs next as a normal turn.
3. Enter on an empty prompt box with nothing just queued does nothing.
4. Stop the agent while a steer is pending: the prompt is not lost.
5. Queue a correction, Enter to steer, Enter a third time: the current turn stops and the correction runs as its own turn.
6. Let the steer be delivered first, then press Enter again: nothing is interrupted and the toast says the agent already has it.
