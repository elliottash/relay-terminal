---
id: 48S3
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: glm/glm-5.3-flashx
session: 4f90df2d-53d3-4240-905f-dafe9b96553b
rank: zzzzzzzzzzzzzzzw
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-48S3-execute-button-pane/], related: [], github: null}
---
# Execute button stays live while a pane is executing the card

## Issue
in the switchboard, the "execute" button is still available when execution is already happening. its supposed to say executing (pane ID) and take you to the pane

## Execution Summary
The card page's Execute button no longer offers a second hand-off while a pane holds the card. While the card carries a session token whose pane is still open and a status still `executing`/`in-progress`, the button reads `Executing (xxxxxxxx)` — the same eight characters the claim chip and the thread's hand-off entries show — and clicking it reveals that pane (`onFocusPane`, the claim chip's own handler) instead of opening another one. Once the card lands or the pane closes, the button is `Execute (x)` again.

Commit 2bfe71aee065 (src/BoardPane.cpp, tests/boardexecute_test.cpp). Evidence: docs/qa_evidence/2026-09-20-48S3-execute-button-pane/.
Follow-up (owner: "same thing is needed for plan and verify"): the Plan and Verify buttons get the same treatment. While a pane is planning the card (live session, status `planning`), Plan reads `Planning (xxxxxxxx)` and reveals the pane; while a verifier pane holds the card in a QA lane (live session, status `needs-qa-*`), Verify reads `Verifying (xxxxxxxx)` and reveals it — with or without a recommended verifier, which only matters for opening a new one. The `p`/`x`/`v` keys go through the same reveal-first branch, so a key on a working card raises the pane too. Commit 70c5be225d93.

## QA checklist
- [ ] With a pane executing a card (claim it from the board, keep the pane open), the card page's action row reads `Executing (········)` and clicking it raises that pane — no second pane opens and no `board_execute` goes out.
- [ ] After the pane closes (or the card leaves Executing), the same button reads `Execute (x)` and executes again.
- [ ] On a narrow card page the executing label sheds gracefully (fitButtons) and never clips.
- [ ] The key legend and the `x` key still execute for a card no pane holds.
- [ ] With a pane planning a card (status Planning, pane open), Plan reads `Planning (········)` and clicking it — or pressing `p` — raises that pane; no second plan starts.
- [ ] With a verifier pane holding a card in a QA lane, Verify reads `Verifying (········)` and reveals the pane, also when the card has no recommended verifier.
- [ ] After the pane closes, Plan/Verify return to `Plan (p)` / `Verify (v)` and behave as before.

## Tests
`ctest --test-dir build -R boardexecute` — new tests/boardexecute_test.cpp: executing state labels the button `Executing (abcdef12)` and the click reveals the pane; a closed pane's claim and a landed card's live claim both revert to `Execute (x)`.
`ctest --test-dir build -R boardpane` — card-page regression suite.
`scripts/relay-build` — full build green; land.py's exact-tree build gate passed.
`ctest --test-dir build -R boardexecute` — adds thePlanButtonNamesThePlanningPaneAndRevealsIt and theVerifyButtonNamesTheVerifyingPaneAndRevealsIt (live claim labels and reveals; closed claim reverts to Plan (p)/Verify (v)).
`ctest --test-dir build -R boardpane` — card-page regression suite.
`scripts/relay-build` — full build green; land.py's exact-tree build gate passed for 70c5be225d93.
