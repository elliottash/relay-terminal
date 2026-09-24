# #WFRA, GUI half: the Verify strip on the card page (2026-09-23)

Two grabs of `BoardView` under `QT_QPA_PLATFORM=offscreen`, taken by
`tests/boardpane_test.cpp::theVerifyStripReadsTheCardsVerifyBlock` when
`RELAY_VERIFY_STRIP_SCREENSHOTS` names this directory:

```
QT_QPA_PLATFORM=offscreen RELAY_VERIFY_STRIP_SCREENSHOTS=docs/qa_evidence/2026-09-23-WFRA-strip \
  ./build/relay-boardpane-tests theVerifyStripReadsTheCardsVerifyBlock
```

- `card-with-verify-block.png`: card #K7Q2 arrived with
  `verify: {artifact: visual, primary: probe, also: [ai-visual, pairwise], human: required,
  criteria: "the strip reads as one line", sign_off: none, effort: medium, stakes: rework,
  blast: capability}`. The strip under the fields and above the body reads
  `Verify: probe · also ai-visual, pairwise · person required: the strip reads as one line ·
  effort medium`, in amber because the plan asks for a person. The rest of the block
  (artifact, sample, sign-off, stakes, blast) is the strip's tooltip.
- `card-without-verify-block.png`: card #SSRQ arrived with no `verify` key. The same strip
  says `No verify plan yet` in the muted ink. The strip is on every card page; only its words
  change.

No button, no editing: the block is the agent's and `board_update_card`'s to write.
