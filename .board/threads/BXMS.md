<!-- relay:entry 20260923T170000Z-b1 author=claude kind=progress -->
### Claude · 2026-09-23 13:00
Claimed. Removing the Models pane's per-pane "use" and adding "all models" / "model settings" to the model box.

<!-- relay:entry 20260923T180500Z-b2 author=claude kind=progress -->
### Claude · 2026-09-23 14:05
Landed 12ef73d7. Moved to needs-verification with evidence in docs/qa_evidence/2026-09-23-box-all-models/.
<!-- relay:entry 20260925T223817Z-53 author=agent kind=note -->
Verification (rev 2db96643, clean worktree): PASSED — this card's named tests green (enterPicksNoPanesModel, theHeaderSaysTheSettingsAreForEveryPane 4/0), modelrows/modelpicker/filterpopup green, box rows + full list confirmed live; the only modelspane failure is #E8V1's stale string (#SYTR). The old theFooterNamesTheRealKeys failure the card recorded is fixed since. Evidence: docs/qa_evidence/2026-09-25-verify-BXMS/. Moved needs-verification → needs-qa-llm.

