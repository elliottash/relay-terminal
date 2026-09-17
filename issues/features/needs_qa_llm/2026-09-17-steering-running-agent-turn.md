# Steering: add a prompt to the running agent turn at its next tool call

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (GUI E1 subagent), 2026-09-17
- **Source**: owner decisions in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`; contract `docs/AGENT-SESSIONS-PROTOCOL.md`

## Behavior as implemented

- **Backend** (`backend/relay_core/queue.py`, `agent.py`, `worker.py`): `ask {when:"steer"}` while a turn runs holds the prompt; the agent loop takes held prompts at the next step boundary (after tool results, before the next model call) and appends them as a user message; `steer_delivered {ids, request_ids}` is emitted. Prompts left when the turn ends emit `steer_returned {id, request_id, prompt, requeued}` and are requeued at the front unless the request had `requeue:false`. When no turn runs, steer behaves like queue. `queue_steer {item}` upgrades a queued item. `queue_changed` lists `steering`. Tests: `tests/test_queue.py` SteerTests (5).
- **GUI:** an agent prompt entered with Enter while the agent works is queued as before (status: "Enter again to send at the next tool call"). Pressing Enter on the empty prompt box within 15 s upgrades the last queued agent prompt: it leaves the queue, is sent as `ask {when:"steer", requeue:false}`, and shows in the strip as "↪ next tool call ✦ …" (purple). On delivery it prints "› prompt ↪ at the next tool call" in the turn. If the turn ends first, Relay puts the prompt back at the head of its own queue and runs it next.

## Implementer check (not a QA verdict)

Under Xvfb with Kimi K3 (`docs/qa_evidence/2026-09-17-steering/`): during "run sleep 6; echo one, then sleep 6; echo two, then summarize", "please also include the current year in your summary" + Enter, Enter showed the steering row (`implementer-strip-next-tool-call-badge.png`); it was delivered after the second command, the agent ran `date +%Y` and included 2026 in the summary (`implementer-steer-delivered-mid-turn.png`).

Not verified live: the returned-to-queue path (covered by backend tests).

## QA checklist

1. Start a multi-step task; queue a correction; Enter again; the agent changes course after its current tool call.
2. Steer during the final answer (no more tool calls): the prompt runs next as a normal turn.
3. Enter on an empty prompt box with nothing just queued does nothing.
4. Stop the agent while a steer is pending: the prompt is not lost.
