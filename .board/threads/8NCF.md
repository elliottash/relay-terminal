<!-- relay:entry 20260920T030949Z-2j author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-bug-agent-often-isnt-assigning-a-summary-title-t.md

<!-- relay:entry 20260920T041055Z-ex author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041517Z-3h author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260922T042554Z-b3 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260922T042554Z-b4 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260922T043215Z-qc author=agent kind=event model=kimi-k3 pane=switchboard turn=c99459b53e3ecc95f3cb78a5d35f317e/2cb42a4f1e994cb9900b97a865671f1e -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T043247Z-7y author=agent kind=event model=kimi-k3 pane=switchboard turn=c99459b53e3ecc95f3cb78a5d35f317e/2cb42a4f1e994cb9900b97a865671f1e -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T043302Z-by author=agent kind=comment mode=plan model=kimi-k3 turn=c99459b53e3ecc95f3cb78a5d35f317e/2cb42a4f1e994cb9900b97a865671f1e -->
I have the worker side. Now the GUI side and existing tests.







No `session_title` handler in the GUI at all — that's suspicious. Let me see how pane titles do get set.

































I have enough. The key finding: a failed title call silently marks the cadence as covered (no retry for 5 turns, no log line). Writing `## Done means` first.



**Planned #8NCF.** The likely cause: `release_title` counts a failed or unusable title call as covered, so the pane keeps its first-prompt fallback for five more turns, and every failure path swallows the exception with no log line — Execute will make failures logged and retried at the next turn end, after first checking whether the GUI even consumes the `session_title` event (`src/Pane.h` was unsearchable in this turn). One open choice is flagged in Risks: retry every turn end until a title lands (the default) versus back off to the 5-turn cadence after one retry.

<!-- relay:entry 20260922T043302Z-da author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260922T044429Z-pg author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session b7a78bf3

<!-- relay:entry 20260922T044429Z-vs author=owner kind=progress pane=switchboard pane_token=b7a78bf3-f266-41da-a607-1a88d0594bc1 -->
Claimed (b7a78bf3) · working on it from a terminal pane

Execute pressed on iOS Safari.

<!-- relay:entry 20260922T044620Z-pe author=agent kind=progress model=gpt-6-astra pane=b7a78bf3 turn=52bf804728cf4665a07750a5af76484a/fa308a84bd3240e4af4861861b0e44e1 -->
Confirmed Pane.h consumes session_title and calls setTitleFromWorker. The first failed title currently advances title_turn, suppressing retries. Implementing the planned cadence fix and reason-only logging, with regression coverage for provider construction failures, call failures and unusable replies.

<!-- relay:entry 20260922T044826Z-mq author=agent kind=progress model=gpt-6-astra pane=b7a78bf3 turn=52bf804728cf4665a07750a5af76484a/fa308a84bd3240e4af4861861b0e44e1 -->
69 targeted Python tests now pass via scripts/test.sh's unittest/JUnit runner (pytest is not installed), including failure retry, recovery, user titles, compaction staleness and guest-side-call diagnostics. Built relay-titles-tests with scripts/relay-build; ctest titles passes. Checking the existing GUI event handler under isolated Xvfb before handoff.

<!-- relay:entry 20260922T044850Z-vp author=agent kind=event model=gpt-6-astra pane=b7a78bf3 turn=52bf804728cf4665a07750a5af76484a/fa308a84bd3240e4af4861861b0e44e1 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-title-ret…; replaced `## Tests`

<!-- relay:entry 20260922T044913Z-51 author=agent kind=event model=gpt-6-astra pane=b7a78bf3 turn=52bf804728cf4665a07750a5af76484a/fa308a84bd3240e4af4861861b0e44e1 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T044937Z-eq author=agent kind=evidence -->
Check · 1 not-applicable, 3 passed; 2 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T044955Z-g1 author=agent kind=event model=gpt-6-astra pane=b7a78bf3 turn=52bf804728cf4665a07750a5af76484a/fa308a84bd3240e4af4861861b0e44e1 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented failure retries and reason-only logging; 69 Python tests, C++ titles test, isolated Xvfb header fixture pass; tests_check passes all three automated entries. · evidence docs/qa_evidence/2026-09-22-title-retries/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T045012Z-zt author=agent kind=event model=gpt-6-astra pane=b7a78bf3 turn=52bf804728cf4665a07750a5af76484a/fa308a84bd3240e4af4861861b0e44e1 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-title-ret… → {"plans": [], "commits": ["0ccc9cd1"], "evidence": ["docs/qa_evidence/2026-09-22…
