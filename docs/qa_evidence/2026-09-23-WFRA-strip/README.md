# #WFRA, GUI half: the Verify strip on the card page (2026-09-23)

Two grabs of `BoardView` under `QT_QPA_PLATFORM=offscreen`, taken by
`tests/boardpane_test.cpp::theVerifyStripReadsTheCardsVerifyBlock` when
`RELAY_VERIFY_STRIP_SCREENSHOTS` names this directory:

```
QT_QPA_PLATFORM=offscreen RELAY_VERIFY_STRIP_SCREENSHOTS=docs/qa_evidence/2026-09-23-WFRA-strip \
  ./build/relay-boardpane-tests theVerifyStripReadsTheCardsVerifyBlock
```

Owner steer 2026-09-23: most of the `verify:` block is the agent's work and the user does
not see it directly, so the strip is shown only when the plan leaves something for a person.

- `card-asking-for-review.png`: card #K7Q2 arrived with
  `verify: {artifact: visual, primary: probe, also: [ai-visual, pairwise], human: required,
  criteria: "the strip reads as one line", sign_off: none, effort: medium, stakes: rework,
  blast: capability}`. Of all that, the strip under the fields link and above the body shows
  one muted line — `Your review: the strip reads as one line · effort medium`. The full block
  (artifact, primary, also, sample, stakes, blast) is the strip's tooltip.
- `card-with-nothing-to-verify.png`: card #SSRQ arrived with no `verify` key. No strip, no
  placeholder — the page is just the card. A fully machine-verified plan (`human: none`,
  `sign_off: none`, no deferral) draws the same nothing, which is the point.

The other shapes the strip takes, covered by the same test: `Unverified until <deferred>`
when the card is knowingly waiting, `Needs your sign-off: <sign_off>` when the sign-off is
more than `none`, and the two or three joined by ` · ` when a plan carries more than one.
No button, no editing: the block is the agent's and `board_update_card`'s to write.
