---
id: XP7N
type: work
status: needs-verification
labels: [feature, agent]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mpx
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [97add146dbe8ce0877e31ad533ea21775860874e], evidence: [docs/qa_evidence/2026-09-21-plan-exit/verification.md], related: [], github: null}
---
# Let the agent request to exit planning mode

## Issue
bug / feature request -- the agent needs to be able to request to exit planning mode

## Plan
**Goal:** Let a Relay agent request to leave planning mode, with the user choosing whether to execute.

**Findings:** `backend/relay_core/planning.py` offers write_plan; `agent.py` enforces plan restrictions. `questions.py` already waits for user choices, and `src/Pane.h` handles mode_changed.

**Steps:**
1. Add a stable exit_plan_mode tool and explain it in the plan instructions.
2. Ask Execute / Keep planning through the existing question flow; switch to build only on explicit Execute. Keep readonly turns protected.
3. Test acceptance, refusal, unanswered, cancellation, invalid arguments and build-mode rejection; document the behavior.

**Risks:** A missing answer must never authorize execution. Reuse existing UI/protocol events and keep the planning provider for the current turn, as existing routing does.

**Verify:** Targeted PlanModeTests and question/plan-turn tests; inspect existing GUI event handlers; manual QA of the ask and PLAN indicator.

## Tests
`tests/test_sessions.py::PlanModeTests`

`PYTHONPATH=backend:tests python3 -m unittest test_sessions test_questions -q`

Related plan-turn regression run: baseline guest prompt assertion failure tracked separately in #GPF7; all other tests passed.

## Execution Summary
Added exit_plan_mode with an Execute / Keep planning ask. Explicit Execute switches to build and emits the existing mode_changed event; the agent can edit in the same turn. Refusal, missing answers, cancellation and question limits never authorize execution. Readonly and unreachable-user checks remain enforced. Tool schemas remain stable across modes.

Evidence: docs/qa_evidence/2026-09-21-plan-exit/verification.md

## QA checklist
- [ ] In a native Relay plan-mode turn, have the agent call exit_plan_mode; verify the inline ask offers Execute and Keep planning.
- [ ] Choose Execute: PLAN clears, Build mode appears, and implementation continues.
- [ ] Repeat with Keep planning and with dismissal: PLAN stays active and edits remain blocked.
- [ ] Stop while the ask is open: the ask closes and no edit runs.
