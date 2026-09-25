<!-- relay:entry 20260924T214551Z-mk author=agent kind=event model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-24-test-queue-test-tier-lists-and-test-failover-fai.md

<!-- relay:entry 20260924T214609Z-2n author=agent kind=evidence model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 -->
Found while verifying #VTJR on 2026-09-24. On a clean `git archive HEAD` export (73644db0) with `PYTHONPATH=backend:tests python3 -m unittest`:

- tests.test_queue.ConsoleFieldTests.test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why — FAIL
- tests.test_tier_lists.StartEffortTests.test_every_cloud_row_carries_both_keys — FAIL ×3 (presets relay-pro-flash, relay-pro-high, relay-pro-main)
- tests.test_failover.FailoverTests.test_the_twin_is_tried_once_even_when_the_list_names_openrouter — FAIL

Identical failures in the shared checkout and on the clean export, so they are at HEAD, not dirty-tree fallout. All look like stale assertions from in-flight features (relay-pro tiers, failover twin, card-turn writer gating), possibly already being fixed by the sessions working those files — check `git status`/`land.py who` before starting.

<!-- relay:entry 20260924T214609Z-8d author=agent kind=event model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
