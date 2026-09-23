---
id: PBKR
type: work
status: needs-verification
labels: [bug, models, agent]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mpbkr
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-plan-restore/README.md], related: [PH9G, PLDG], github: null}
---
# Leaving Plan restores the previous model selection

## Issue
yeah but leaving plan mode didnt swtich it back

## Done means
- Leaving Plan restores the role, model and effort that were active before entering it.
- Repeated Plan requests do not overwrite the saved selection; a pane already on High returns to High.
- Manual exits, worker-reported exits and entering/exiting before configuration preserve the selection and conversation.

## Plan
**Goal:** Make the automatic High selection temporary.
**Findings:** `Pane::setAgentMode` selects High on entry and never restores anything; the #PH9G test explicitly expects High after exit.
**Steps:** Save the prior selection once, restore it through the existing model-switch protocol on exit, and cover configured, lazy and worker-driven transitions in real Pane tests. Update protocol documentation.
**Risks:** Repeated events must not reapply a stale restore. Existing model-switch machinery preserves conversation and may defer a switch while a guest is running.
**Verify:** Build and run the focused consolemode tests with isolated settings; run guest handover regressions.

## Execution Summary
The pane snapshots its pre-plan role, model and effort once. Manual exits restore before set_mode; worker mode_changed exits restore once through the existing model-switch protocol. Main uses set_model; other roles use set_agent_role with an explicit pick. No model names are hard-coded, and no conversation is reset. Real Pane tests cover all four roles, repeated entry, duplicate exit events, and exit before configuration. Screenshots use scripted worker events, not live provider calls.

![Restored Main selection after Plan](docs/qa_evidence/2026-09-22-plan-restore/03-restored.png)

Evidence: docs/qa_evidence/2026-09-22-plan-restore/README.md

## Tests
- `scripts/relay-build --target relay-consolemode-tests -j2` — passed.
- `XDG_CONFIG_HOME=$(mktemp -d) xvfb-run -a build/relay-consolemode-tests --plan-click-only` — passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_guest_handover` — 9 passed.
- manual: docs/qa_evidence/2026-09-22-plan-restore/README.md (real Pane screenshots with scripted worker events).
- Exact proposed commit built and passed --plan-click-only in land.py's isolated tree.
- Full shared-checkout app build blocked by unrelated Actions palette edits: RelayWindow.h references undeclared togglePalette/m_palette. Reported on #MAGP; /tmp/planback-app-build.log.
