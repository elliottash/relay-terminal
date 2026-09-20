---
id: 8EJ4
type: work
status: inbox
labels: [bug, switchboard, tests]
rank: zzzzzzzzzzzzzzz
created: '2026-09-20'
source: 'pane eacfa50b executing #CYM9, 2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [CYM9], github: null}
---
# five stale board tests fail at HEAD: policy wording, stage lifecycle, import

## Issue
found while executing #CYM9: five tests in tests/test_board_tools.py and tests/test_board_protocol.py fail on a clean worktree of HEAD (verified at 2967c4c0 and again at 6011141c with none of #CYM9's changes): test_the_policy_ships_next_to_the_module_and_names_the_rules (the policy no longer says 'needs-qa-llm'), test_other_writes_are_capped_per_turn (3 != 2 thread entries), test_read_caps_the_thread_tail (6 != 5), test_apply_creates_the_ticked_cards_and_says_where_they_landed, test_a_card_detail_read_round_trips_through_the_protocol. They look like fallout from the board_policy.md rewrite and the #3XZV stage lifecycle; two stable ones were fixed in e7279bf8/cfe58e2c, these five remain.
