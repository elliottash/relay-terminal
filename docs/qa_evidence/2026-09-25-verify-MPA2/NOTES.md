# Verify MPA2 — Available models missing from Priorities (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). Both linked commits (3cb7ff33, 8d03da03) are ancestors — the same landing #CDP7 shares.

## Tests (run this sweep in the clean worktree; identical binaries/suites)
- `ctest -R ^modelcatalog$` — **73/0** (fills this card's missing-evidence line).
- `ctest -R ^modelpicker$` — **62/0**.
- `ctest -R ^modelspane$` — **25/1**, the #E8V1 stale string (#SYTR).
- `tests/test_openrouter_catalog.py` — **1 failed** (#Y4PJ later drift; green at the card's own commit; filed #042V with checkout proof at 78639301, which contains these two commits).
- `tests/test_guest_harness_provider.py` — **1 failed** (later-added harness; #042V).
- manual `docs/qa_evidence/2026-09-23-MPA2/README.md` — **present**, with ctest/guest/openrouter/stage logs.

## Done means
- Reproduce or explain the absence — **passed by evidence**: the README documents the two reproduced defects (pane-catalog divergence for Priorities; late guest-harness catalog delivery) and the fixes in 3cb7ff33/8d03da03.
- Correct behavior with a focused regression — **passed by test evidence**: the modelcatalog/modelpicker regression cases (late delivery, tier membership) are green at HEAD; live drives this sweep list the full codex/openrouter catalogs in Enabled and Order immediately.

## Verdict
PASS (shares CDP7's landing; the two python failures are later drift, #042V).
