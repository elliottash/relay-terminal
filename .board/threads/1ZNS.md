<!-- relay:entry 20260924T031812Z-nv author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/7f4e6db975974bbcbdbfb576af6f2691 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-23-router-false-positive-sentence-starting-with-a-n.md

<!-- relay:entry 20260924T031834Z-wa author=agent kind=evidence model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/7f4e6db975974bbcbdbfb576af6f2691 -->
Reproduced: `classify("claude has usage rests now, …")` → Decision(route='shell', reason='Runnable shell command.'). Chain: (1) no NATURAL prefix; (2) check_runnable passes because `claude` resolves on PATH (~/.npm-global/bin/claude) and the rest are plain words; (3) the only prose safety net for runnable input, assist_signals(), returns (0, [], 'claude') because its first gate is `first not in ENGLISH_COMMANDS → score 0` — the assist only covers first words that are both commands and everyday English (make/find/look); (4) 0 < ASSIST_THRESHOLD 2 → shell. The sentence itself is saturated with signals: has/we/should/those/my/the/in/so/now would score 14 if the first-word gate didn't bail.

<!-- relay:entry 20260924T031834Z-yy author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/7f4e6db975974bbcbdbfb576af6f2691 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260924T031834Z-yz author=agent kind=question model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/7f4e6db975974bbcbdbfb576af6f2691 -->
Fix it? Recommended: keep the English-command path as-is, and for a runnable first word that is NOT in ENGLISH_COMMANDS, still score the args and route to agent at a higher bar (e.g. score ≥ 4 plus sentence punctuation or a pronoun), so `claude --help`, `git commit -m …`, `docker ps` stay shell (flags/operators already bail) but "claude has usage rests now, so we should …" reads as the request it is. Would add corpus tests around agent-tool names (claude, codex, gemini, cursor).

<!-- relay:entry 20260924T031843Z-pd author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/7f4e6db975974bbcbdbfb576af6f2691 -->
- ✦ agent moved this card · unchanged · Diagnosis delivered; waiting on owner's go-ahead for the fix shape

<!-- relay:entry 20260924T031855Z-cg author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/7f4e6db975974bbcbdbfb576af6f2691 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260924T032237Z-m1 author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by kimi/kimi-k3, session ebabd73d

<!-- relay:entry 20260924T032237Z-m2 author=agent kind=progress model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd pane_token=ebabd73d-d0ae-4b24-81b1-f988035fe913 -->
Claimed (ebabd73d) · working on it from a terminal pane

Implementing the approved fix: score args after a non-English runnable first word, with a higher bar (≥4 + punctuation or weight-2 word), plus corpus tests.

<!-- relay:entry 20260924T032257Z-9x author=agent kind=decision model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd -->
Owner approved the recommended fix: "yes, implement that" — score args after a non-English runnable first word, higher bar (≥4 plus punctuation or a weight-2 word), corpus tests around agent-tool names.

<!-- relay:entry 20260924T032257Z-9y author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off…; replaced `## Done means`

<!-- relay:entry 20260924T033552Z-qa author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd -->
- ✦ agent updated this card · waiting_on: owner → (unset); replaced `## Execution Summary`

<!-- relay:entry 20260924T033604Z-j4 author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260924T033617Z-wb author=agent kind=event model=kimi-k3 pane=ebabd73d turn=1fe719161a35499b8808944cdbe9d507/48068a6f15ba44579de0e554fbffaafd -->
- ✦ agent moved this card · Executing → Done, session ebabd73d released · Medium fix landed in c4955c5; verify met by script: 48 router tests + 21 lang_router tests OK, owner-reported sentence now routes to agent with needs_assist, invocations stay shell · verified_by kimi/kimi-k3
