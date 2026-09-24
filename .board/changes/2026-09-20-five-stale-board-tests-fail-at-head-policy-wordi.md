---
id: 8EJ4
type: work
status: planned
labels: [bug, switchboard, tests]
rank: zzzzzzzzzzzzzzz
created: '2026-09-20'
source: 'pane eacfa50b executing #CYM9, 2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [CYM9], github: null}
---
# five stale board tests fail at HEAD: policy wording, stage lifecycle, import

## Issue
found while executing #CYM9: five tests in tests/test_board_tools.py and tests/test_board_protocol.py fail on a clean worktree of HEAD (verified at 2967c4c0 and again at 6011141c with none of #CYM9's changes): test_the_policy_ships_next_to_the_module_and_names_the_rules (the policy no longer says 'needs-qa-llm'), test_other_writes_are_capped_per_turn (3 != 2 thread entries), test_read_caps_the_thread_tail (6 != 5), test_apply_creates_the_ticked_cards_and_says_where_they_landed, test_a_card_detail_read_round_trips_through_the_protocol. They look like fallout from the board_policy.md rewrite and the #3XZV stage lifecycle; two stable ones were fixed in e7279bf8/cfe58e2c, these five remain.

## Done means
The five tests named in the issue (three in tests/test_board_tools.py, two in tests/test_board_protocol.py) pass on a clean checkout of HEAD, either because later commits already repaired them or because this card updates their expectations to the behaviour the board_policy.md rewrite and the #3XZV stage lifecycle deliberately chose. Failure looks like: any of the five still red on a clean export, or a test "fixed" by weakening an assertion that was actually guarding correct behaviour (e.g. a real cap or protocol round-trip regression papered over instead of fixed in the code).

## Plan
**Goal**

Make the five stale board tests named in the issue green on a clean checkout of HEAD, by aligning their expectations with the behaviour the board_policy.md rewrite and the #3XZV stage lifecycle intentionally introduced — not by changing product behaviour to match stale tests, and not by weakening assertions that guard real behaviour.

**Findings**

- The five tests: `tests/test_board_tools.py` — `test_the_policy_ships_next_to_the_module_and_names_the_rules` (line ~110), `test_read_caps_the_thread_tail` (~186), `test_other_writes_are_capped_per_turn` (~1760); `tests/test_board_protocol.py` — `test_a_card_detail_read_round_trips_through_the_protocol` (~492), `test_apply_creates_the_ticked_cards_and_says_where_they_landed` (~2411).
- The card was filed 2026-09-20 against HEAD 6011141c; the tree has moved a lot since. Planning-time greps suggest several may already be fixed:
  - `backend/relay_core/board_policy.md` no longer contains `needs-qa-llm` anywhere, and the policy test at line 113 now carries a comment citing the owner's 2026-09-20 decision that removed it from rule 5 — i.e. the test looks updated since the card was written.
  - `test_other_writes_are_capped_per_turn` now asserts `len(thread) == 3` (line 1768) — the "3 != 2" failure direction, so likely already corrected.
  - `test_read_caps_the_thread_tail` now asserts `len(result["thread"]) == 2` (line 191), a different shape than the "6 != 5" failure — possibly rewritten.
- So step 1 decides how much work this card actually is. Do not assume all five still fail.
- Related: #3XZV (stage lifecycle) is the change whose fallout these tests are; its own thread records the earlier fixes in e7279bf8/cfe58e2c and confirms the new wording/lifecycle are the intended behaviour.

**Steps**

1. Verify the current state on a clean export of HEAD (this checkout is shared and dirty by design): `git archive HEAD | tar -x -C <scratch>`, then run the five tests there, e.g. `cd <scratch> && python3 -m pytest tests/test_board_tools.py tests/test_board_protocol.py -k 'policy_ships or caps_the_thread_tail or capped_per_turn or ticked_cards or round_trips_through_the_protocol' -x -q`. Record which of the five still fail.
2. If all five pass: no code change. Note the confirming run in `## Execution Summary`, and the card is done (small-work outcome; say in the reply that the failures were already repaired since 6011141c).
3. For each test that still fails, read the assertion and the code under test (`backend/relay_core/board_tools.py`, `board_protocol.py`, `board_policy.md`) and decide which side is stale:
   - Policy wording test: assert against the current rule wording in `backend/relay_core/board_policy.md` (post 2026-09-20 decision: any pane may flip a card once the verdict section exists; `needs-qa-llm` is no longer named in rule 5). The test belongs to the policy, so the test moves.
   - Thread-cap tests (`caps_the_thread_tail`, `capped_per_turn`): check what the caps actually are now in `board_tools.py` (default `thread_entries` tail on read; per-turn write cap and its thread-entry accounting). If the cap is an intentional product value, update the expectation; if the count changed because a new automatic thread entry is being appended (e.g. a stage-move event from #3XZV), prefer asserting the new intentional count with a comment naming its source.
   - Protocol round-trip and import-apply tests: these guard round-trip fidelity, which must not be weakened. If they fail, suspect a real regression in `board_protocol.py` detail-read or import-apply response shape first; fix the code, not the test, unless the response shape was deliberately extended (then extend the assertion).
4. Keep each fix minimal: only the failing expectations, with a short comment when the new number/wording is the product of a named change (#3XZV, policy rewrite).
5. Re-run the five tests on the clean export again; then also run the two full files `python3 -m pytest tests/test_board_tools.py tests/test_board_protocol.py -q` in the working checkout to catch knock-on breakage.

**Risks**

- The failures may be fully healed already (findings above), in which case the card closes with a confirming run and no commit — that is a fine outcome.
- The shared checkout is dirty; conclusions drawn from running tests in it directly are untrustworthy. Always confirm on a `git archive HEAD` export.
- If a round-trip failure turns out to be a real protocol regression rather than stale expectations, the fix is product code in `board_protocol.py` — still in scope for this card, but say so plainly in `## Execution Summary`.

**Verify**

- `python3 -m pytest tests/test_board_tools.py tests/test_board_protocol.py -k 'policy_ships or caps_the_thread_tail or capped_per_turn or ticked_cards or round_trips_through_the_protocol' -q` green on a clean export of HEAD.
- Full `tests/test_board_tools.py` and `tests/test_board_protocol.py` green (no knock-on breakage).
- No assertion weakened without a comment naming the intentional change it tracks.
