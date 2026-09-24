# #BX7B Review pane evidence

`01-review-queue.png` is an offscreen capture of the actual `ReviewPane` widget from
`ReviewPaneTests::answerUsesTheRevisionRead`. Its fixture has one research result requiring
a person's judgement, plus goal, pass criterion, measured result and evidence path. The pane
shows the question and one answer field; the agent's verify block does not appear.

Capture:

```bash
scripts/relay-build --target relay-reviewpane-tests
QT_QPA_PLATFORM=offscreen \
  RELAY_REVIEW_SCREENSHOTS=docs/qa_evidence/2026-09-24-BX7B-review \
  ./build/relay-reviewpane-tests answerUsesTheRevisionRead
```

The same test asserts that recording the answer sends `board_update` with the card's
`base_hash` and an `Answer:` line naming the reviewed revision. It also simulates a
stale-write refusal and checks that the person's draft stays in the field while the pane
re-reads the card. `ctest --test-dir build -R '^(boardpane|reviewpane)$'` checks the Board
action and the Review pane together.
