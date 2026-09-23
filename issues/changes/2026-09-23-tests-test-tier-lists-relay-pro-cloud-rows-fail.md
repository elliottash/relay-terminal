---
id: NYXV
type: work
status: inbox
labels: [bug, models, tests]
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# tests.test_tier_lists: relay-pro cloud rows fail test_every_cloud_row_carries_both_keys (3 subtests, pre-existing at HEAD)

## Issue
Found while delivering #Y4PJ: `PYTHONPATH=backend python3 -m unittest tests.test_tier_lists.StartEffortTests.test_every_cloud_row_carries_both_keys` fails 3× at HEAD (and unchanged after #D0MC/##Y4PJ) — subtests (preset='relay-pro', model='relay-pro-high' | 'relay-pro-main' | 'relay-pro-flash'): the relay-pro cloud rows do not carry both keys the test requires. Pre-existing on main; unrelated to the tier-defaults change (verified identical in a clean HEAD export in /tmp).
