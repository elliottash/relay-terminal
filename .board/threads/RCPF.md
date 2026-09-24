<!-- relay:entry 20260921T001846Z-dt author=agent kind=decision model=kimi-k3 pane=switchboard turn=b01394da11cb49fea974b728ea889ed4/c4f4c58d805647b4887292ca2172c1b8 -->
Owner, on #PH0N: "oh yeah, thats fine." — Recap on a keyless desktop may spend Relay Free through the summaries role. The question this card asks is answered; also recorded in #PH0N's `## Decisions`.

<!-- relay:entry 20260924T042950Z-9x author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T042950Z-ys author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Planning · a Plan turn started

<!-- relay:entry 20260924T043536Z-x9 author=agent kind=event model=kimi-k3 pane=switchboard turn=81e3e1eb58cd996dbdb4a99968e68b07/890dee01a17244e3bf0dac5ea118113d -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043609Z-ey author=agent kind=event model=kimi-k3 pane=switchboard turn=81e3e1eb58cd996dbdb4a99968e68b07/890dee01a17244e3bf0dac5ea118113d -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043617Z-dm author=agent kind=comment mode=plan model=kimi-k3 turn=81e3e1eb58cd996dbdb4a99968e68b07/890dee01a17244e3bf0dac5ea118113d -->
I'll read the card in full and then the code it points at.











The file's too big for direct reads; I'll pull the relevant parts with targeted searches.











































I have the full picture now. Re-reading the card for its current hash, then writing the sections.





**Plan written on #RCPF.** Since the owner already decided a recap may spend Relay Free, the plan leaves recap behaviour alone and builds the Discussion's tail: a `RELAY_HOSTED=off` env switch gated in `hosted.py` (`available()`, `Session.token()`, `status()`), exported by `scripts/relay-qa-run` so QA drives are offline-by-default, with tests in `tests/test_hosted.py` and the roles/tier tests plus a loopback-stub verification drive. Nothing is needed from the owner — the card is ready to Run.

<!-- relay:entry 20260924T043617Z-dn author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
