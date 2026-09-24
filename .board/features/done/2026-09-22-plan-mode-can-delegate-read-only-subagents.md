---
id: P1DG
type: work
status: done
labels: [feature, subagents, plan-mode]
assignee: claude-code
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: mpldg
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22'
links: {plans: [], commits: [e5b9eff6], evidence: [], related: [K3TY, GMCF, SBGN, XP7N, Z0VG, PH9G, HR5E, PMX7], github: null}
---
# Plan mode locks nothing: an instruction plus /high

## Issue
i got a note that delegation was disabled in plan mode, can you check that? agents are supposed to be able to delegate subagents to help with planning. (this might have already been fixed)

## Decisions
Owner, 2026-09-22, after e5b9eff6 made plan-mode subagents read-only: "i thought i wanted nothing to be locked in plan mode. its just an extra instruction to the agent plus high reasoning" · "*higher effort ; that depends on what users want."

Owner, later the same day: "re the effort. thats not what its supposed to do.its suppsoed to go into /high" · "check for any model-specific hard-coding, which i really dont want." · "it doesnt need to be the same model either".

So plan mode refuses no tool. It is the plan-mode note on the turn plus `/high`: entering plan mode puts the pane on the High tier (#PH9G), whose list picks the model and level, any model. A plan turn adds no swap or effort boost of its own; only a hand-pinned `roles.planning` routes one. The read-only-subagent rule of e5b9eff6 is withdrawn. This supersedes the K3TY thread's "nothing changes before Execute" and #XP7N's "an edit before the call is still refused".

## Done means
- In plan mode every tool is callable: `write_file`, `edit_file`, `set_keybinding`, board and app writes, `agent`/`agent_message`/`agent_wait` (native and through the guest bridge), and subagents get their ordinary tools.
- The plan-mode note still tells the agent to investigate without changing anything and finish with `write_plan`.
- Read-only survey turns and card turns keep their own refusals, and start no subagent through the native batch path (which used to skip `_prepare`).

## Execution Summary
**Third pass (plan = /high).** `RoleResolver._default("planning")` is now the pane as it is, so `planning_target()` is None unpinned; `Agent._begin_plan_turn` no longer rebuilds a resolver to boost the selected model, and `_begin_guest_plan_boost` (#HR5E's per-turn top-level stage for a guest pane) is removed with its restore in `_end_plan_turn`. A failed `guest:` pin now leaves the pane as it is. The Options › Models "plan mode" row, `roles.ACTIONS`, protocol §6/13/13.11, ARCHITECTURE and test comments now say plan mode puts the pane on /high. Hard-coding audit: no plan or /high logic branches on a model name (backend or GUI); the only model-shaped behaviour was that per-turn top-level boost, now gone. Model names remain only in catalog data (`presets.py` tiers/presets, the vision/route-assist default tables in `roles.py`, voice's default, codex's offline model list in `guest_harness_provider.py`).


First pass (e5b9eff6) let plan mode delegate to read-only subagents. Per the owner's decision, this pass removes every plan-mode lock: `planning.PLAN_BLOCKED_TOOLS` is gone, `Agent._prepare` no longer refuses board, app or executor tools in plan mode, and `SubagentManager` is back to its pre-#P1DG code. `READONLY_BLOCKED` now lists its write tools itself instead of borrowing the plan list. The batch gate for read-only and card turns from e5b9eff6 stays. Protocol §6 and the `write_file` paragraph say plan mode locks nothing.

## Tests
- Third pass: `tests/test_plan_turns.py` (`test_an_unpinned_plan_turn_is_the_panes_own_turn`, `test_a_pane_that_is_itself_a_guest_plans_at_its_own_level`, pinned-swap tests), `tests/test_roles.py` `PlanRoleTests`, `tests/test_tier_lists.py`; green with test_presets, test_guest_harness_provider, test_model_switch, test_configure_recovery, test_sessions, test_failover, test_questions, test_session_protocol, test_subagents.
- `tests/test_subagents.py`: `test_plan_mode_delegates_like_build_mode`, `test_a_read_only_turn_starts_no_subagent`
- `tests/test_guest_delegation.py`: `test_plan_mode_spawns_children`, `test_readonly_and_card_scope_cannot_spawn`
- `tests/test_sessions.py`: `test_plan_mode_tools_and_write_plan` (write_file now runs), `test_exit_plan_mode_switches_to_build_and_enables_edits_in_the_same_turn`, `test_plan_mode_keeps_the_subagent_tools_in_the_list_and_allows_them`
- `tests/test_app_tools.py`: `test_plan_mode_locks_nothing`; `tests/test_guest_board_bridge.py`: `test_plan_readonly_stop_and_next_turn`
- Also green: test_todo_subagents, test_plan_turns, test_questions, test_board_turns, test_queue, test_board_chat, test_card_model_selection, test_board_tools, test_agents_defs, test_session_protocol, test_failover, test_hosted, test_customproviders, test_remote_board.
