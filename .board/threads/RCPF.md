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

<!-- relay:entry 20260925T115320Z-xa author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session 4386a69e

<!-- relay:entry 20260925T115320Z-xb author=owner kind=progress pane=switchboard pane_token=4386a69e-3006-48f5-bb9c-6e7338a8c7df -->
Claimed (4386a69e) · working on it from a terminal pane

Run pressed on iOS Safari.

<!-- relay:entry 20260925T115659Z-dz author=agent kind=event model=claude-opus-5-5 pane=4386a69e turn=d55449d2a1e34f9aa7c193f74c36a6d0/81539e9ee1dc476484d7452376c0cff3 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-21-ph0n-host… → {"plans": [], "commits": ["15ee6c5"], "evidence": ["docs/qa_evidence/2026-09-21-…; waiting_on: owner → (unset); appended to `## Tests`

<!-- relay:entry 20260925T115706Z-hx author=agent kind=event model=claude-opus-5-5 pane=4386a69e turn=d55449d2a1e34f9aa7c193f74c36a6d0/81539e9ee1dc476484d7452376c0cff3 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260925T115707Z-hy author=agent kind=event model=claude-opus-5-5 pane=4386a69e turn=d55449d2a1e34f9aa7c193f74c36a6d0/81539e9ee1dc476484d7452376c0cff3 -->
- ✦ agent moved this card · Executing → Needs verification · RELAY_HOSTED=off landed in 15ee6c5. Tests and a live stub drive show no gateway request with the switch set, and relay-qa-run defaults it on. · evidence docs/qa_evidence/2026-09-25-rcpf-hosted-off/ · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T115710Z-64 author=agent kind=progress model=claude-opus-5-5 pane=4386a69e turn=d55449d2a1e34f9aa7c193f74c36a6d0/81539e9ee1dc476484d7452376c0cff3 -->
Landed `15ee6c5`: `RELAY_HOSTED=off` switches Relay Free off in a profile, and `scripts/relay-qa-run` now defaults it on. With it set, a loopback stub gateway logged no requests from any entry point: chat, recap roles, key test, Relay Pro, image and health. With it unset, Relay Free works as before. The evidence is in `docs/qa_evidence/2026-09-25-rcpf-hosted-off/`. Two test failures also fail on a clean HEAD export, so this card didn't cause them; both are listed under `## Tests`. Moved to needs-verification.
