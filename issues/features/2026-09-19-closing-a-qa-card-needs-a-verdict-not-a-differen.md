---
id: 76DJ
type: work
status: in-progress
labels: [feature, switchboard]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzy
created: '2026-09-19'
source: pane 1, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Closing a QA card needs a verdict, not a different model family

## Issue
can you change that rule -- flipping to done doesnt require a different pane, just that its verified

## What changed
Owner, 2026-09-20: closing a QA card is gated on the verifier's verdict in the body, not on the closing pane being a different model family. `board_tools.move_card` drops the closer/implementer family refusal; the verdict-section requirement and the Relay-Free refusal (owner, 2026-09-19) stay, and `verified_by` still stamps whoever closes. Wording updated in the move tool's description, `board.py`, `board_policy.md` (the agent rules text), SWITCHBOARD-DESIGN.md and SWITCHBOARD-FORMAT.md. Tests: the same-family close now succeeds and stamps `verified_by` (also for a relay-free-implemented card); the no-verdict and relay-free-closer refusals unchanged.

## QA checklist
Evidence: `docs/qa_evidence/2026-09-20-verdict-not-family-close/` (notes + test output).

- [ ] `tests.test_board_tools` (148) and `tests.test_board_protocol` + `tests.test_tool_labels` + `tests.test_board_import` (258) pass.
- [ ] A card in a QA lane with a `## Verdict` closes to `done` from the same model family that implemented it, and `verified_by` stamps the closer (`test_the_same_model_family_closes_it_once_the_verdict_is_there`).
- [ ] Closing without a verdict section is still refused (`requires: verdict`), and Relay Free still may not close anything (`requires: independent_model`, "not available on Relay Free").
- [ ] No stray promise of the old rule survives: `rg -n "independence|must not be the family" backend/ docs/SWITCHBOARD-*.md` shows only historical notes (#T71W history, card references).
- [ ] The agent rules text (`board_policy.md` rule 5) and the `board_move_card` tool description state the new rule.
