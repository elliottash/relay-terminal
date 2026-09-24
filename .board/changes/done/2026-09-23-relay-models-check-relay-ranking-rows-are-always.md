---
id: D0MC
type: work
status: done
labels: [bug, models, tests]
assignee: agent
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
links: {plans: [], commits: [928705019522e6453a34f3d7afac36f44f541b84], evidence: [], related: [Y4PJ], github: null}
---
# relay-models check: relay-* ranking rows are always "unserved" — served compares display names to the ranking ids

## Issue
Found while delivering #Y4PJ: `python3 -m unittest tests.test_relay_models` fails 5/19 at HEAD because `scripts/relay-models.py check` always reports the Relay Free ranking rows as unserved ("model-ranking.md: relay-flash is classed flash, but no provider's catalog serves a model by that name", same for relay-lite and relay-main). The check's `served` set holds `name_of(p, row['id'])` — display names — and `presets.model_name('relay-free', 'relay-main')` is 'relay free · main', so the ranking file's row ids relay-main/relay-flash/relay-lite can never match. False positive: presets.MODEL_CATALOG['relay-free'] serves exactly those ids. Breaks CheckTests.test_the_shipped_tree_has_only_the_known_drift and four SetTests via assert_only_known_drift.

## Done means
`python3 scripts/relay-models.py check` no longer prints the three `relay-*` "no provider's catalog serves a model by that name" lines, and `PYTHONPATH=backend python3 -m unittest tests.test_relay_models` goes green (19/19; 5 fail at HEAD). Failure would show as the relay-* lines still appearing or any other new drift line in check output.

## Execution Summary
The check's `served` set now counts a MODEL_CATALOG row's id as well as its display name (`scripts/relay-models.py`): the ranking file names rows by id, but relay-free's display names are "relay free · main" for relay-main, so the relay-main/relay-flash/relay-lite rows could never match before. The three false-positive lines are gone from `check` output; the suite module that was 5/19 red is 19/19 OK. Landed with this card.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_relay_models` — 19/19 OK (5 failures at HEAD).
`python3 scripts/relay-models.py check` — no relay-* "unserved" lines (only the three #Y4PJ drift lines, until that card lands).
