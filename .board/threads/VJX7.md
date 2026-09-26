<!-- relay:entry 20260925T194227Z-0m author=agent kind=event model=k3 pane=f3ec7a75 turn=6d73ff44e1d94bfc9037568464418adb/db91409d11ad4ed0a9f2b4403d272f26 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-remote-router-command-shaped-lines-classify-as-a.md

<!-- relay:entry 20260925T195540Z-9a author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T195540Z-sq author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T195800Z-99 author=agent kind=event model=k3 pane=switchboard turn=08cc83fc49541250f04345441f305a73/d005bd3cd10f45f1946066065df74b27 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T195820Z-z8 author=agent kind=event model=k3 pane=switchboard turn=08cc83fc49541250f04345441f305a73/d005bd3cd10f45f1946066065df74b27 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T195832Z-6d author=agent kind=question model=k3 pane=switchboard turn=08cc83fc49541250f04345441f305a73/d005bd3cd10f45f1946066065df74b27 -->
Product call the plan hinges on: does the #1ZNS sentence rule outrank typing command-shaped lines on the remote host, or should remote mode keep the stricter command reading? Recommendation: keep remote stricter — remotely nothing can check whether "a", "b" or "pods" name files, and a command silently sent to the agent is a no-op — while still assisting on sentence punctuation or a trailing "?", which keeps #1ZNS's motivating case ("claude has usage rests now, so we should …") routed to the agent remotely too. If you want #1ZNS to outrank, the fix is to change the remote test's expectations instead.

<!-- relay:entry 20260925T195839Z-zf author=agent kind=comment mode=plan model=k3 turn=08cc83fc49541250f04345441f305a73/d005bd3cd10f45f1946066065df74b27 -->
Planning card #VJX7. Let me read the card and the relevant code first.



The router lives in `relay_core/router.py`. Let me read the classification logic, especially the remote path and the #1ZNS sentence rule.



Now let me look at `assist_signals`, `SIGNAL_WORDS` and `ASSIST_THRESHOLD` — the likely cause of the misclassification.



Let me check the `NATURAL` regex and the git history context of the #1ZNS change, then write the plan.

One more look at how the local path applies the assist, to keep the plan accurate.





I have the full picture. Re-reading the card to get its current `hash`, then writing `## Done means`.







**Plan written on #VJX7.** The diagnosis is confirmed: since c4955c52 (#1ZNS), `assist_signals` no longer zeroes the score for a non-English first word, and at a remote prompt "cp a b" (article + lead-in "a" = 4) and "kubectl get pods in the namespace" ("in" + "the" + word count = 4) both clear `NON_ENGLISH_THRESHOLD` and get sent to the agent. The plan tightens the remote assist gate so it fires only on evidence no invocation has — sentence punctuation or a trailing "?" — which restores the shell routing for command-shaped lines while keeping #1ZNS's real case working remotely.

It needs one thing from you before Run: the product call the issue names, posted as a question on the card with a recommendation (remote keeps the stricter reading). If you disagree, the fix becomes a test-expectation change instead.

<!-- relay:entry 20260925T195839Z-zg author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T200600Z-n4 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session a5ccfd18

<!-- relay:entry 20260925T200600Z-n5 author=owner kind=progress pane=switchboard pane_token=a5ccfd18-92ab-4215-b969-c40fdfb108b3 -->
Claimed (a5ccfd18) · working on it from a terminal pane

<!-- relay:entry 20260925T200649Z-rb author=agent kind=event model=claude-opus-5-5 pane=a5ccfd18 turn=af2bd9d50a4647e683626f082940c77c/7c4ffa2e36054b26bb265075244689f0 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["b0e017e4e676", "de31a0136f2a"], "evidence": [], "rela… → {"plans": [], "commits": ["b0e017e4e676", "de31a0136f2a"], "evidence": ["docs/qa…; appended to `## Tests`

<!-- relay:entry 20260925T200652Z-am author=agent kind=event model=claude-opus-5-5 pane=a5ccfd18 turn=af2bd9d50a4647e683626f082940c77c/7c4ffa2e36054b26bb265075244689f0 -->
- ✦ agent moved this card · Executing → Needs verification · Landed de31a013: remote assist after a non-English command name needs punctuation or a trailing '?'; RemoteRouterTests and test_router green (57). · evidence docs/qa_evidence/2026-09-25-remote-router-command-lines/ · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T200654Z-ws author=agent kind=progress model=claude-opus-5-5 pane=a5ccfd18 turn=af2bd9d50a4647e683626f082940c77c/7c4ffa2e36054b26bb265075244689f0 -->
Landed de31a013. New `_remote_assist_signals` in router.py, which `_classify_remote` uses for both its agent signal and its assist gate. At an ssh prompt, a line that starts with a non-English command name only assists when it has sentence punctuation or a trailing "?". "cp a b", "gti status" and "kubectl get pods in the namespace" now route to shell. "make the tests pass" and #1ZNS's comma sentence still go to the agent. Local classify is unchanged. I took the plan's recommendation on the product call (remote keeps the stricter reading) because the open question had no answer. Note: land.py also appended this hash to #1ZNS's links.commits because the commit message mentions it.

<!-- relay:entry 20260925T215602Z-96 author=agent kind=decision model=k3 pane=2e4cc4a0 turn=af2bd9d50a4647e683626f082940c77c/faf5f10aeb7a4f13a7e63d1dfb75e0a6 -->
Owner answered the product question the plan hinged on: "i want the sentence rule for remote sessions -- build and test it out" (terminal pane, 2026-09-25). #1ZNS's sentence rule outranks typing command-shaped lines on the remote host; the fix is the test-expectation change, not the remote-only tightening landed in de31a0136f2a.

<!-- relay:entry 20260925T215619Z-mw author=agent kind=event model=k3 pane=2e4cc4a0 turn=af2bd9d50a4647e683626f082940c77c/faf5f10aeb7a4f13a7e63d1dfb75e0a6 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T215632Z-8k author=agent kind=event model=k3 pane=2e4cc4a0 turn=af2bd9d50a4647e683626f082940c77c/faf5f10aeb7a4f13a7e63d1dfb75e0a6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["b0e017e4e676", "de31a0136f2a", "2f3834c1e73f"], "evid… → {"commits": ["b0e017e4e676", "de31a0136f2a", "2f3834c1e73f"], "evidence": ["docs…; replaced `## Tests`

<!-- relay:entry 20260925T215643Z-zd author=agent kind=progress model=k3 pane=2e4cc4a0 turn=af2bd9d50a4647e683626f082940c77c/faf5f10aeb7a4f13a7e63d1dfb75e0a6 -->
Landed 2f3834c1e73f implementing the owner decision: `_remote_assist_signals` removed, `_classify_remote` uses the same assist gate as local. "cp a b" and "kubectl get pods in the namespace" now ask first (route agent, needs_assist) at a remote prompt; "kubectl get pods", "git status" stay shell; #1ZNS's comma sentence still goes to the agent remotely. 97 tests pass (PYTHONPATH=backend python3 -m unittest tests.test_ssh_remote tests.test_router). Done means, Tests and evidence link updated to the revised rule; evidence docs/qa_evidence/2026-09-25-vjx7-sentence-rule-remote/. Card stays in needs-verification for a separate verifying session.
