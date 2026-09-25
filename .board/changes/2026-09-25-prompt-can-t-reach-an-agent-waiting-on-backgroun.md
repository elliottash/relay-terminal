---
id: T4VK
type: work
status: needs-verification
labels: [bug, agents, queue]
assignee: agent
implemented_by: glm/glm-5.3
session: d53ee6b1-5068-4c69-bc11-e9cc04fb441e
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: the pytest run and the pane unit test both pass, sign_off: none, effort: low}
source: pane 1, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-27-t4vk-steer-wakes-agent-wait/], related: [], github: null}
---
# Prompt can't reach an agent waiting on background agents without interrupting

## Issue
While a turn is blocked in agent_wait (or a command_output job wait), a steered prompt is never delivered: steers land at the next step boundary, and a parked wait has none, so the user must double-Enter to interrupt. Make a pending steer wake the wait (delivered right after the tool result), and let a submitted prompt steer into a waiting turn instead of queueing.

> apparent bug: when a pane is waiting for background agents, i cant send messages, i have to do the 2x enter to interupt. i think you should be able to send messages, like how it woks in claude code
> — elliott · [session:0b9295b56ae5446fbc563a5065aa88f6](relay://session/0b9295b56ae5446fbc563a5065aa88f6) · 2026-09-25

## Done means
- A prompt submitted while the running turn is blocked in `agent_wait` (or waiting on `command_output` for a background job) reaches the model without the user interrupting the turn: one Enter steers it in, the parked wait returns early, and the message joins the conversation right after that tool result.
- Background agents and jobs are untouched by the wake: they keep running; only the wait ends early, and the tool result says so.
- With older queued prompts ahead, behaviour is unchanged (queue order kept; Enter-on-empty still steers the first queued item, and now that steer also lands).
- Worker tests: a steer pending during `agent_wait` ends the wait and the steered text is the next user message; no steer, no early return. Pane test: `queuesubmit::decide` returns the steer decision exactly for a busy agent waiting on background work with an empty queue.

## Plan
**Goal** A user can talk to an agent that is parked waiting for background agents, as in Claude Code, without the double-Enter interrupt.

**Findings**
- Steers are delivered only at step boundaries (`backend/relay_core/agent.py` run loop, `self.steer_source()` ~2747); a turn blocked in `agent_wait` (`backend/relay_core/subagents.py` `Subagents._wait` ~1226, 0.05 s poll) or in `command_output` (`backend/relay_core/tools.py` `_await` → `backend/relay_core/jobs.py` `wait` ~279) has no step boundary until the wait ends, so steers pend forever — the user's only path is the Enter/Enter interrupt.
- The pane queues a submitted prompt whenever `m_agentBusy` (`src/QueueSubmit.h` `decide`, `src/Pane.h` `submitAgent`/`enqueue`); the steer offer is empty-box Enter (`emptyEnterSteerSequence`). The pane already tracks the live wait calls: `m_waitCall` (agent_wait) and `m_jobWaitCall` (command_output), cards #V7QD/#KP4M.
- The queue's steer store is `backend/relay_core/queue.py` (`take_steer` drains at the step boundary).

**Steps**
1. `queue.py`: non-draining `peek_steer()`; wire `agent.steer_peek` beside `steer_source` in `set_agent`.
2. `agent.py`: hold `steer_peek`; pass a wake callable into `subagents.run_tool` for `agent_wait` and into the tool executor for `command_output`.
3. `subagents.py`: `_wait`/`wait` accept the wake; a pending steer returns early with a `stopped_for_user_message` note (not `timed_out`); agents keep running. `jobs.py`/`tools.py`: same wake for `command_output`; the existing still-running note covers it.
4. `QueueSubmit.h`: `State::agentWaitingBackground` + `Decision::Steer` for busy agent + background wait + empty queue; `Pane.h` sets the flag from `m_waitCall`/`m_jobWaitCall` and `submitAgent` enqueues then steers on that decision.
5. Tests: pytest for the wake (fake clock/poll), C++ unit for `decide`.

**Risks** A steer peeked but not drained (turn ends first) follows existing queue handling; `timed_out` must stay false for a woken wait so the model does not misread it.

**Verify** `python3 -m pytest` on the new worker test; pane test via `scripts/relay-build --target <test>` + `ctest -R`.

## Execution Summary
Landed `218d8fea` on main (verify-slot build of the exact tree passed). Worker: `Queue.peek_steer()` (non-draining `take_steer`) wired as `agent.steer_peek`; `Subagents.wait/_wait` and `Jobs.wait` take a `steer_wake`, end the wait early when a steer is pending, and `agent_wait`'s result carries `stopped_for_user_message` + a note instead of `timed_out` — subagents and jobs keep running. The executor gets the wake for `command_output` (both the plain wait and the `from_line` re-read wait); a subagent's executor deliberately does not. Pane: `queuesubmit::State` gains `agentWaitingBackground` (from `m_waitCall`/`m_jobWaitCall`) and `agentQueueEmpty`; `decide` returns `Decision::Steer` for a busy turn parked on background work with an empty agent lane, and `submitAgent` enqueues then steers that entry — one Enter, no interrupt. Queue order is kept: only the lane's head steers; with prompts queued ahead the submit still queues and the empty-Enter steer delivers the first of them (and that steer now lands too). Foreground `run_command` waits are untouched.

## Tests
- `tests/test_subagents.py` `HandoffTests.test_steer_ends_agent_wait_and_lands_next_step` — a steer submitted 0.3 s into a gated `agent_wait` ends the wait (`stopped_for_user_message`, `timed_out` false, subagent `running`), the steered text is the next user message the model sees, and no second turn starts. **pass** (52/52 in `tests.test_subagents`)
- `HandoffTests.test_agent_wait_without_a_steer_still_waits_the_timeout` — no steer means the wait keeps its deadline: `timed_out` true, no `stopped_for_user_message`. **pass**
- `tests/queuesubmit_test.cpp` — Steer decided for a busy turn parked on background work; Queue when an agent prompt is queued ahead; Queue (never Steer) while a turn is still starting. **pass** (`ctest -R queuesubmit`)
- Neighbours green: `ctest -R "subagents|queuenav"`, `tests.test_tools`, `tests.test_terminal_handoff`, `tests.test_queue` minus two pre-existing tree failures unrelated to this change (`test_tool_outcomes` import of `test_agent`; #CTRN wording in `board_tools.py`, a file this change never touched).
- Evidence: `docs/qa_evidence/2026-09-27-t4vk-steer-wakes-agent-wait/EVIDENCE.md`

## Try it
**Open:** `sh docs/qa_evidence/2026-09-25-tryit-T4VK/stage.sh`

The script prints, with timestamps, an agent turn that spawns a background subagent and parks in `agent_wait`; at t+0.5 s you "type" a message and press Enter. Read the timeline it prints: when the wait gives way, what the wait reports, what the model sees next, and how many turns ran. About 2 minutes.

Is one Enter — the message reaching the parked agent while the background work simply keeps running — the behaviour you wanted, and does the wait's `stopped_for_user_message` result read clearly enough for the agent to act on?

Expected: docs/qa_evidence/2026-09-25-tryit-T4VK/expected.md (sealed until you answer)
