---
id: XP7N
type: work
status: needs-verification
labels: [feature, agent]
assignee: agent
implemented_by: kimi/kimi-k3
session: 8d16eb6c-8bf3-4b06-acab-9e886d6621bd
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
`exit_plan_mode {reason}` (plan mode only) leaves plan mode on the agent's own decision, Warp-style: it validates the reason, switches the session to build mode at once, emits the existing `mode_changed {mode: "build"}` event, and the turn continues with build tools — no question flow. An edit before the call is still refused; one after it succeeds in the same turn. Readonly turns and build-mode calls are refused; the schema is identical on every request of the turn (prompt cache). First landed as an Execute / Keep planning ask (97add146), reworked per the owner's 2026-09-21 decision.

Evidence: docs/qa_evidence/2026-09-21-plan-exit/verification.md

## QA checklist
- [ ] In a native Relay plan-mode turn, have the agent call exit_plan_mode: no ask appears, the PLAN indicator clears, Build mode shows, and implementation continues in the same turn.
- [ ] The reason text is visible with the tool call in the pane.
- [ ] Edits attempted before exit_plan_mode are still refused; edits after it run.
- [ ] A readonly plan turn cannot call exit_plan_mode.

## Decisions
- 2026-09-21, owner: "or actually, i woudl like it if the agent could decide itself ot leave planning mode, more like warp" — exit_plan_mode switches to build mode on the agent's own decision; the Execute / Keep planning ask from the first implementation is removed.

## Tasks

