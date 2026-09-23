---
id: 3BPH
type: work
status: discussing
labels: [bug, board]
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-22'
source: pane a32ee1d3, 2026-09-22, found during /clean-commit validation
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# boardexecute test fails at HEAD: Execute/Plan/Verify buttons never appear in the open card

## Issue
/clean-commit
`tests/boardexecute_test.cpp` (committed at 70c5be22, #48S3) fails all 3 of its cases once it is actually built and run: the open card page shows no `boardExecute`/`boardReplyButton` button, so `actionButton()` returns null (`'button' returned FALSE`, lines 119/161/198). It never ran before because the CMakeLists.txt wiring was left uncommitted. Reproduced identically at pristine HEAD (4e6e483b) with only the wiring applied — see docs/qa_evidence/2026-09-22-boardexecute-fails-at-head/notes.md. Either the button rendering regressed after the tests were written or the tests need the card page in a state they are not producing.
