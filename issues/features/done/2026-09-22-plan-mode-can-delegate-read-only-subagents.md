---
id: PLDG
type: work
status: done
labels: [feature, subagents, plan-mode]
assignee: claude-code
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: mpldg
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [K3TY, GMCF, SBGN], github: null}
---
# Plan mode can delegate to read-only subagents

## Issue
i got a note that delegation was disabled in plan mode, can you check that? agents are supposed to be able to delegate subagents to help with planning. (this might have already been fixed)

## Done means
- In plan mode `agent`, `agent_message` and `agent_wait` are no longer refused, on the native batch path and through the guest bridge.
- A subagent started in plan mode has no `write_file`/`edit_file`, its prompt says it is read-only, and a guest child gets `deny` permissions.
- Plan mode cannot message (and so resume) a subagent that can write.
- A read-only survey turn and a card turn still start no subagent; failure shows as a `subagent_started` event in those turns.

## Execution Summary
Not already fixed: #GMCF had put the three agent tools in `PLAN_BLOCKED_TOOLS`, and since #SBGN removed `explore` there was no read-only type left to make an exception for. The K3TY thread had settled that plan turns should run read-only subagents and deferred it to a separate card; this is that card.

`planning.PLAN_BLOCKED_TOOLS` drops the agent tools and the plan note says subagents are read-only. `SubagentManager.spawn/start_batch/run_tool` take `read_only`; a read-only spawn replaces the definition with one without file writes (which also yields the read-only prompt line and guest `deny`), and `agent_message` from plan mode refuses a writable target. `agent.py` passes `read_only=self.mode == "plan"`, keeps the agent tools in `READONLY_BLOCKED` (it was derived from the plan list), and no longer starts a native batch in a read-only or card turn — that path skipped `_prepare`, so those turns could already spawn before.

## Tests
- `tests/test_subagents.py`: `test_plan_mode_delegates_to_read_only_subagents`, `test_a_read_only_turn_starts_no_subagent`
- `tests/test_guest_delegation.py`: `test_plan_mode_spawns_read_only_children`
- `tests/test_sessions.py`: `test_plan_mode_keeps_the_subagent_tools_in_the_list_and_allows_them`
- Also green: test_todo_subagents, test_plan_turns, test_questions, test_agents_defs, test_guest_board_bridge, test_board_chat, test_queue, test_board_turns, test_card_model_selection, test_board_tools.
