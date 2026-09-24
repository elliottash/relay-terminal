# #495G subscription-aware routing — implementation evidence

Commits: `e1ddd0b2` (effective availability and lifecycle draws), `1d1cf772` (quota handoff).

## Checks

- `PYTHONPATH=backend python3 -m unittest tests.test_roles`: 79 passed.
- `QT_QPA_PLATFORM=offscreen build/relay-modelcatalog-tests -silent`: 71 passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_failover tests.test_guest_harness_provider tests.test_roles`: 215 ran, one unrelated failure in `test_the_twin_is_tried_once_even_when_the_list_names_openrouter`; the test expects the retired `deepseek/deepseek-v4.1-flash` fallback. This failure also occurs in isolation.
- New focused quota tests: three API handoff and three guest handoff tests passed. They cover same-service account retry, partial output, a prior guest tool action, failover off, and no reset redemption.
- `scripts/relay-build --target relay`: passed. The land gate also built the exact tree for both commits.
- Synthetic seeded draw: two Claude account snapshots at the same rank had scores 10 and 5 percentage points/hour. Over 10,000 draws, they were selected 6,625 and 3,375 times. No live subscription request or reset credit was used.

## Behavior to verify in Relay

Open new panes and change modes with two eligible accounts at the same rank; selections should favor the higher remaining-per-hour score while still sometimes choosing the other. After a real quota refusal, an allowed second account should continue a safe turn. If answer text or a guest tool action already occurred, the current turn stops and the next turn routes around the exhausted account. Relay never spends a reset credit automatically.
