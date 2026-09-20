<!-- relay:entry 20260920T190025Z-v2 author=agent kind=comment -->
Filed from the terminal; research on comparable systems in progress before the questions are written.

<!-- relay:entry 20260920T192334Z-jp author=agent kind=question -->
Questions for the owner, each with a recommendation (detail in docs/SIGNALS-RESEARCH.md):

1. Files in git, or a local store with only promoted cards in git? **Recommend: local store** (`issues/.private/signals/events.jsonl`). Cost: signals are per machine.
2. Is a signal a `type: signal` card at all? **Recommend: no** — a keyed record drawn on the board, which gets a card id only on promotion; a card type would inherit ids, threads, ranks, check, GitHub sync and the survey, each of which would then be switched off.
3. Open on the first failure or the second? **Recommend: pending on the first, open on the second consecutive failing execution**; Relay re-runs up to ten failed tests once, so this takes seconds.
4. Two resolve numbers, 2 passes for broken and 20 for flaky? **Recommend: yes.**
5. What promotes a signal to a bug card? **Recommend:** the agent gave up, or three failing runs over at least 24 hours unclaimed, or confirmed flaky. Never age alone; at most five promoted cards open.
6. When the card and the signal disagree, which wins? **Recommend: the signal** (reverses my first proposal).
7. May an agent dismiss a signal? **Recommend: yes, for `environmental` and `flaky-known` only**, with a comment and an expiry of at most 7 days; `wont-fix`, `expected` and longer expiries stay with you; every dismissal expires.
8. Should an open signal block a card's verification? **Recommend: only a signal first seen in a run by the pane holding that card**; older ones are listed as "open before this card" and never block.
9. Do agents pick up unclaimed signals unasked? **Recommend:** a pane is always told about signals its own runs produced; unclaimed ones are worked only by an explicit sweep, three at a time, under the board's `autonomy`.
10. Which sources after tests? **Recommend:** `check` problems and build failures in the first version, then crashes, then CI; lint only with a baseline; QA verdicts never.
11. Fold into #7BM4 or stay its own card? **Recommend: its own card**, after #7BM4 step 1, and strike "flaky tests become cards" there.
12. The name. **Recommend: keep "signal"**; "alert" would promise a notification, which a signal must not send.
