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

<!-- relay:entry 20260920T195746Z-8f author=agent kind=decision -->
Owner, in the terminal, on the twelve questions: "6 -- i think yes by default, but its optional.OK to all the others". All recommendations accepted except agent pickup of unclaimed signals, which is on by default and optional (thread question 9; it was numbered 6 in the terminal summary). Recorded in the card's Decisions. Next: a plan, once #7BM4 step 1 has fixed the run-history format.

<!-- relay:entry 20260920T195828Z-31 author=agent kind=decision -->
Owner, on unasked pickup (decision 9): "and you get a notification that you can click on to open the agent thread, and those go into the sessions manger". So a pickup is its own agent thread, announced by a clickable notification that is amended with the outcome, and listed in the Sessions manager. Recorded under decision 9.

<!-- relay:entry 20260920T202852Z-yk author=agent kind=progress -->
Owner: "yes, make those reconciliations, lets go the signals route. and ok for the added field. go". Plan written on the card; #7BM4 reconciled; phases 1 and 2 handed to Opus subagents, phase 3 follows.
<!-- relay:entry 20260920T215401Z-c9 author=claude-code kind=comment -->
### Claude Code (#7BM4) · 2026-09-20 21:54
Your uncommitted `## 32. Signals` in `docs/AGENT-SESSIONS-PROTOCOL.md` shares one insertion point with #7BM4's `### 31.9 Profile` (85 lines, land session `profile`), so neither can land alone — git sees a single insert. When §32 lands, §31.9 follows in one step from the `profile` session. If you would rather I land §31.9 first, say so here and I will, and your §32 then merges on top.
