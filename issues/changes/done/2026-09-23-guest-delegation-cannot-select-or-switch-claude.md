---
id: 7XN0
type: work
status: done
labels: [bug, guest, subagents]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: Codex in Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [tests/test_subagents.py], related: [], github: null}
---
# Guest delegation cannot select or switch Claude Opus subagents

## Issue
fix this:

There is a Relay limitation behind the conversion request: the guest delegation bridge exposes agent and agent_message, but not
agent_set_model. Also, its opus delegation alias currently resolves to “inherit,” so simply passing model: "opus" would not
reliably select Opus. This needs a Relay fix; Fable did not successfully convert those agents.

## Done means
A guest pane can call `agent_set_model` to switch one or all existing subagents; the change is visible in their model state. A Claude guest pane that spawns or switches with `model: "opus"` selects Claude Code's Opus model, rather than inheriting Fable. Invalid targets and missing arguments fail clearly; existing explicit aliases keep working.

## Plan
Goal: allow guest agents to choose Claude Opus for new or existing children.
Findings: `subagents.py` omits `agent_set_model` from tool specs and routes `opus` through the generic inherit alias; `guest_board_bridge.py` omits the switch tool from its allowlist.
Steps: (1) Add a validated `agent_set_model` tool routed through the existing manager method and expose it to guests. (2) Resolve `opus` to the Claude guest's Opus model while preserving an explicit alias override. (3) Add focused manager and bridge tests and update the protocol note.
Risks: shared checkout has unrelated edits in the bridge and subagent files; snapshot and land only this task's hunks. An already running child changes on its next model step.
Verify: targeted Python tests for subagent choice and guest bridge dispatch; board check.

## Tests
`PYTHONPATH=backend:. python3 -m unittest tests.test_subagents tests.test_guest_board_bridge`
`PYTHONPATH=backend:. python3 -m py_compile backend/relay_core/subagents.py backend/relay_core/guest_board_bridge.py backend/relay_core/agent.py tests/test_subagents.py tests/test_guest_board_bridge.py`
`python3 scripts/relay-board.py check`

## Execution Summary
Exposed `agent_set_model` through the subagent tool list and guest delegation bridge. A Claude Code guest now resolves `model: "opus"` to the Opus CLI model for both new children and model switches; explicit user aliases still take precedence. Added a guest bridge dispatch test and a child spawn/switch regression test. The affected Python test modules passed (61 tests); Python compilation and Board validation passed.
