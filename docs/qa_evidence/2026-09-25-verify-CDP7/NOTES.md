# Verify CDP7 — Codex appears in Priorities (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). All three linked commits (3cb7ff33, 8d03da03, 78639301) are ancestors.

## Tests (clean worktree, this sweep)
- `ctest -R '^modelpicker$'` — **62 passed, 0 failed** (card's own check line: passed).
- `ctest -R '^modelcatalog$'` — **73 passed, 0 failed** (card's check line: missing evidence — now supplied).
- `ctest -R '^modelspane$'` — **25 passed, 1 failed**; the 1 is the #E8V1 stale string (#SYTR), postdating the card's 2026-09-23 green check.
- `PYTHONPATH=backend python3 -m unittest tests.test_openrouter_catalog` — **1 failed** (`test_catalog_rows_puts_the_built_in_tier_rows_first_then_the_live_ones`: 'flash' != 'main'). **Not this card's regression**: the suite runs fully green at CDP7's last commit 78639301 (verified by checkout); `b759f23d` (#Y4PJ defaults move) broke the expectation later. Filed as #042V.
- `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider` — **1 failed** (`test_preset_rows`: third harness `guest:codex:ashe-ethz-ch` added later without updating the test; also green at 78639301). Part of #042V.
- Manual README `docs/qa_evidence/2026-09-22-CDP7/README.md` — **present**.

## Behavior at HEAD (live, this sweep's drives)
- Codex models list and tick in the Enabled tab (`../2026-09-25-verify-HJ1T/07-after-burst.png`: gpt-6-astra/sol/luna, gpt-5.6-sol/terra/luna, gpt-5.5 under "codex"); the account delivers its catalog without a restart (fresh-profile drives list them immediately).
- Search by provider/model across openrouter's long tail is exercised by the green modelpicker suite.

## Verdict
PASS — the two python failures are later drift (#042V), verified green at this card's own commit.
