---
id: VASY
type: work
status: inbox
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
