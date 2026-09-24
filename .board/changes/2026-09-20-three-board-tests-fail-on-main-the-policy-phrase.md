---
id: VASY
type: work
status: planned
labels: [bug, switchboard]
rank: zzzzzzzzzzzzzzzy
created: '2026-09-20'
source: 'pane, 2026-09-20, found while executing #DPJB'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Three board tests fail on main: the policy phrase and an extra thread entry per write

## Issue
[noticed while landing #DPJB, not asked for by the owner] Three board tests are red on main: the policy no longer says needs-qa-llm, and every write appends one more thread entry than the tests expect.

## Evidence
Found on the shared tree at 2026-09-20 while executing #DPJB, with `git diff HEAD` showing nothing of the finder's in these two areas. Reproduce with:

```
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH="$PWD/backend" \
  python3 -m unittest tests.test_board_tools 2>&1 | grep -E '^(FAIL|ERROR):'
```

```
FAIL: test_other_writes_are_capped_per_turn  (LimitTests)      AssertionError: 3 != 2
FAIL: test_read_caps_the_thread_tail         (ListAndReadTests) AssertionError: 6 != 5
FAIL: test_the_policy_ships_next_to_the_module_and_names_the_rules (SpecTests)
      AssertionError: 'needs-qa-llm' not found in Switchboard rules (…)
```

- The two `!=` ones are the same shape: one create plus N writes leaves one *more* thread entry than the test counts, i.e. every write now appends an entry the tests predate (the last commits touching `backend/relay_core/board_tools.py` are 47bb59fa #CYM9 and 56b921f6 #HKAP, both 2026-09-20, both of which append entries).
- The policy one is a stale assertion: `backend/relay_core/board_policy.md` lost the phrase `needs-qa-llm` in 4f5acd43 (#3XZV, 2026-09-19, "move to needs-verification … QA lane") while `SpecTests` still asserts it.

Neither is touched by the finder's change; both are red on HEAD. `tests.test_board_protocol` also fails to load as `tests.test_board_protocol` (loader error) — not chased here.

## Done means
The reproduce command in `## Evidence` runs green on main: `tests.test_board_tools` shows no FAIL/ERROR lines for `test_other_writes_are_capped_per_turn`, `test_read_caps_the_thread_tail`, or `test_the_policy_ships_next_to_the_module_and_names_the_rules`. The checks in that file match the board behaviour and policy text as they stand on main today — every assertion that had to change points at the commit or owner decision that made the old expectation stale (47bb59fa #CYM9, 56b921f6 #HKAP, 4f5acd43 #3XZV), not the other way round. Failure looks like: any of the three tests still red, or a test "fixed" by weakening an assertion that was actually catching a real regression.

## Plan
**Goal.** Make the three red board tests from `## Evidence` agree with main — by confirming the failures are already fixed, or by updating stale test expectations to the behaviour the named commits and owner decisions established.

**Findings.** The card was filed 2026-09-20; the test file has moved since. In `tests/test_board_tools.py` today: line 113 carries a comment "`needs-qa-llm` left rule 5 with the owner's decision of 2026-09-20" and the `SpecTests` phrase list (line 118) asserts `needs-verification`, not `needs-qa-llm` — `backend/relay_core/board_policy.md` indeed says `needs-verification` and never `needs-qa-llm` (confirmed by search). The two count assertions also read differently from the card's failure output: line 1768 asserts `len(self.board.thread(card_id)) == 3` (the card's failure was `3 != 2`) and line 194 asserts `thread_total == 6` (the card's failure was `6 != 5`). So all three failures look already repaired by a later session; the plan is verification-first, fix only if still red. The extra thread entry per write is intended behaviour (`backend/relay_core/board_tools.py` module docstring, line 18: "Every write appends a thread entry"), and the policy wording change 4f5acd43 (#3XZV) was the owner's QA-lane decision — so if anything is still red, the fix goes in the tests, not in `board_tools.py` or `board_policy.md`.

**Steps.**
1. Re-run the reproduce command from `## Evidence` on current main (`git log --oneline -3` first, per the shared-checkout rules).
2. If all three tests pass: find the fixing commits (`git log -L` or `-S 'needs-qa-llm' -- tests/test_board_tools.py`, and blame on lines 113, 194, 1768), record them in `## Execution Summary`, and close the card as already-fixed — no code change.
3. If `test_the_policy_ships_next_to_the_module_and_names_the_rules` still fails: update the stale phrase assertion to the policy's current wording (`needs-verification`), keeping the line-113-style comment naming 4f5acd43/#3XZV as the reason.
4. If either count test still fails: confirm the extra thread entry traces to 47bb59fa (#CYM9) or 56b921f6 (#HKAP) — both intentional write-records — then bump the expected count by one in the test, with a comment naming the commit. Do **not** change `board_tools.py` to suppress the entry.
5. Separately check whether `tests.test_board_protocol` still fails to load (noted in `## Evidence`, out of scope here). If it does, file a new bugs card with the loader error; do not fix it under this card.

**Risks.** The failure modes disagree about direction: the count failures could in principle be a real off-by-one regression in `board_tools.py` that someone papered over in the tests. Step 4 guards this — the executor must name the commit that introduced the entry and confirm it is a deliberate write-record, not accidental double-append. No owner decision needed; the policy direction was already decided (owner, 2026-09-20, cited in the test comment).

**Verify.** The reproduce command from `## Evidence` exits with no `FAIL:`/`ERROR:` lines. If any test file was edited, run the full `tests.test_board_tools` module (not just the three tests) to catch neighbouring-count breakage. `git status` before and after confirms only `tests/test_board_tools.py` changed, if anything.
