---
id: GD8K
type: work
status: needs-verification
labels: [feature, guest, subagents]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in Relay, 2026-09-21
links: {plans: [], commits: [d40085fc90b07f39a6783a266a89b5c4b60c7ce5], evidence: [docs/qa_evidence/2026-09-21-guest-delegation/results.txt], related: [4NXH], github: null}
---
# Codex delegates through Relay so its tasks and child sessions are visible

## Issue
631f6a0f146f4616987d58715d4afa6f

see here, i asked codex to do subagents. i guess it did it internally right in its own harness? is there a way to force codex to use relay's task / subagent system, so that i can observe what they are doing

## Decisions
"great, lets do that. and then tell me what other tools that relay native agents get that codex doesnt"

## Plan
Goal: Codex guest delegation uses Relay-owned tasks and child sessions.

Findings: guest_board_bridge.py exposes only five board tools; SubagentManager already owns child lifecycle, transcripts, task links and UI events. Codex reports features.multi_agent as a supported stable setting.

Steps:
1. Expose agent, agent_message, agent_wait and update_todos through the pane-owned bridge, preserving native stage/cancellation checks and request deduplication.
2. Make bridge launches asynchronous and waits bounded; carry task state and child results into guest turns. Disable Codex native multi-agent features on bridge-enabled start/resume/fork and update guest guidance.
3. Test delegation, task links, result delivery, cancellation, restrictions and configuration; document remaining native tools unavailable to guests.

Risks: guest tool discovery occurs before the worker finishes configuring; background completion must not lose results; shared checkout has other edits in guest adapter files, so land only this work.

Verify: targeted bridge, guest adapter and subagent tests; inspect UI events and saved child transcripts through a fake model, without spending a live model turn. Manual QA: after restarting the worker, delegate from a Codex pane and inspect task/subagent views.

## Execution Summary
Implemented Relay-owned guest delegation and task tracking through relay_board. Codex launch/start/resume/fork disables its internal multi-agent features. Children use Relay's existing manager, status/events, task links, transcripts, cancellation and follow-ups; inherited guest models start lazily and close cleanly. Child tool activity survives in saved transcripts, and pending reports no longer replace the parent's new user prompt.

Remaining native tools are inventoried in docs/GUEST-TOOLS.md. Restart the worker/guest to load the bridge; existing Codex-owned child sessions are not migrated.

Evidence: docs/qa_evidence/2026-09-21-guest-delegation/results.txt. Live-model/GUI verification remains for the verifier; no paid guest turn was run.

## Tests
`tests/test_guest_delegation.py::DelegationTests::test_task_child_events_result_and_saved_transcript`
`tests/test_guest_delegation.py::DelegationTests::test_inherited_guest_child_is_lazy_resumes_and_closes`
`tests/test_guest_delegation.py::DelegationTests::test_stopping_guest_closes_harness_and_routes_questions`
`tests/test_guest_delegation.py::CodexDelegationTests::test_launch_and_start_resume_fork_disable_internal_agents`
manual: docs/qa_evidence/2026-09-21-guest-delegation/results.txt

Regression command: PYTHONPATH=backend python3 -m unittest tests.test_guest_delegation tests.test_guest_board_bridge tests.test_guest_harness_codex tests.test_guest_harness_provider tests.test_subagents -q — 182 passed. Two existing subagent tests now synchronize on the fake provider's first request rather than the earlier running event, removing races in their next-step/transcript assertions.

## QA checklist
- Restart the Relay worker and start/resume a Codex guest; discover agent, agent_message, agent_wait and update_todos from relay_board, with Codex's native spawn_agent unavailable.
- Ask for two independent delegated reviews; confirm linked task rows, live subagent count and separate child transcripts.
- Open a child mid-run and after completion; confirm tool activity and final report remain visible.
- Send a follow-up to a completed child and stop another running child; confirm results and task state match.
- Confirm ordinary board operations still work and a workspace without a board still permits delegation.
- Read docs/GUEST-TOOLS.md for the tool inventory and verification boundary.
