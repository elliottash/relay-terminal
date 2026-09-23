---
id: XP7N
type: work
status: needs-verification
labels: [feature, agent]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
session: 8d16eb6c-8bf3-4b06-acab-9e886d6621bd
rank: mpx
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [97add146dbe8ce0877e31ad533ea21775860874e, 9dc211b4995aad98cb38269a3bfceb864c79f59a], evidence: [docs/qa_evidence/2026-09-21-plan-exit/verification.md, docs/qa_evidence/2026-09-23-guest-plan-exit-XP7N/verification.md], related: [], github: null}
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

**Guest follow-up (2026-09-23).** Goal: let a guest pane or guest planning route call Relay's `exit_plan_mode` and continue in Build within the same turn. Findings: `guest_board_bridge.py` exposes a limited tool set; `guest_harness_provider.py` ignores Relay's native schemas; `planning.GUEST_PLAN_NOTE` asks for a final plan reply; routed guests start with deny permissions even though current Plan mode locks nothing. Steps: (1) expose `write_plan` and `exit_plan_mode` through the guest bridge and existing Agent policy path; (2) align guest Plan instructions and posture with the current native Plan behavior, and avoid saving an implementation reply as a plan; (3) test bridge calls, routed guest transition, and native regressions. Risk: ensure a readonly turn still rejects exit and a guest can write after the mode change. Verify: targeted bridge and plan-turn tests plus a live guest check if the harness is available.

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


## Done means
- In a guest agent pane, `exit_plan_mode` is discoverable and changes Relay from Plan to Build, with `mode_changed` emitted.
- A guest selected for a Plan turn can save its plan, exit Plan, and continue implementation in the same turn; its final implementation reply is not saved as a plan.
- Readonly turns and Build-mode calls remain refused, and targeted native Plan behavior still passes.
