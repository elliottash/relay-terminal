<!-- relay:entry 20260924T185308Z-60 author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/63ed2f602c58425fbc1a38ad70a4eb22 -->
- ✦ agent created this card in Discussing · issues/design/2026-09-24-bundled-skills-what-ships-with-relay-today-what.md

<!-- relay:entry 20260924T185340Z-rm author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/63ed2f602c58425fbc1a38ad70a4eb22 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; component: (unset) → ["worker", "skills"]; replaced `## Discussion points`

<!-- relay:entry 20260924T193711Z-8a author=agent kind=event model=claude-fable-5-1 pane=a9bee86b turn=fc4b907796bb418994c523cffca15623/696ad6f977634c91b72bac9bb900e6a0 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["9HS0", "1QKM", "XHXX",… → {"plans": [], "commits": [], "evidence": ["reports/Bundled skills for Relay.md"]…; appended to `## Discussion points`

<!-- relay:entry 20260924T220109Z-db author=agent kind=question model=claude-fable-5-1 pane=5af07f6f turn=fc4b907796bb418994c523cffca15623/81c9c6409e214a97aafc362e526d603f -->
Decisions this research asks for (details and evidence in reports/Bundled skills for Relay.md, summary in Discussion points):

1. Which skills ship. Recommendation: only operating-Relay skills, plus a Relay `skill-creator`; never a skill that drives another product's UI or holds a credential Relay does not own. Mail becomes a task plugin (`mail_*` tools, credential in the keystore) with a thin bundled skill over it, filed as its own design card.
2. Identity and hiding. Recommendation: dedupe by content hash (`cases.skill_version`), hide by tool availability from a declared `requires` (with the two inferences for undeclared skills), stop discovering Warp's `remote-server/bundled_resources` tree by default, and omit the guest's own home tree from the guest catalogue.
3. `requires` on skills. Recommendation: yes, as a `requires: |` block in the task plugin's shape (program, env, secret, tool), checked at index time; a missing tool hides, a missing program or secret lists with "needs X"; a profile with `money: yes` or `sign_off: send` makes the skill explicit-invocation only.
4. The catalogue line. Recommendation: `short:` else first sentence, `MAX_PROMPT_BYTES` to 8 KiB.
5. Whether to file the four delivery cards in report §5 now (requires and hiding; catalogue line; the bundle as servers with a release test; mail as a plugin).

<!-- relay:entry 20260926T021641Z-zh author=agent kind=event model=z-ai/glm-5.3-flashx pane=2914f43d turn=fc4b907796bb418994c523cffca15623/e1f52a7cede249a69cc58f1b36d1ba39 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["reports/Bundled skills for Relay.md"]… → {"commits": ["b74afc4f68e6", "c1a9585ff165"], "evidence": ["reports/Bundled skil…

<!-- relay:entry 20260926T021701Z-tz author=agent kind=event model=z-ai/glm-5.3-flashx pane=2914f43d turn=fc4b907796bb418994c523cffca15623/e1f52a7cede249a69cc58f1b36d1ba39 -->
- ✦ agent updated this card · appended to `## Discussion points`

<!-- relay:entry 20260926T022046Z-jq author=agent kind=event mention=AVQH model=k3 pane=71f587f3 turn=eaaabcdd1da44126b2adbaae69c43226/0e715b4bb7084033b53821556aff3c45 -->
mentioned in #AVQH · 2026-09-26 · agent

<!-- relay:entry 20260926T022506Z-mc author=agent kind=decision model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
Owner approved the recommendations and extended identity/import: "yes to all the recs. 

but for 2, we should de-dupe semantically as well right? we should have a skill maintenance skill, that runs first when you install relay, it brings in your skills from other agents. but it will also maintain your skill catalog."
Interpretation for delivery: exact hash duplicates collapse automatically; semantic equivalents are proposed as candidates for review, preserving each source, version and differing instructions until the owner chooses a canonical skill. First-run skill maintenance inventories other agents' skills and offers import; later runs review duplicates, broken requirements and stale skills.

<!-- relay:entry 20260926T022512Z-b1 author=agent kind=event model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
- ✦ agent updated this card · waiting_on: owner → (unset); appended to `## Decisions`

<!-- relay:entry 20260926T022527Z-rr author=agent kind=event mention=K26R model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
mentioned in #K26R · 2026-09-26 · agent

<!-- relay:entry 20260926T022528Z-30 author=agent kind=event mention=G8JN model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
mentioned in #G8JN · 2026-09-26 · agent

<!-- relay:entry 20260926T022529Z-5r author=agent kind=event mention=4EMF model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
mentioned in #4EMF · 2026-09-26 · agent

<!-- relay:entry 20260926T022530Z-rj author=agent kind=event mention=M91Y model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
mentioned in #M91Y · 2026-09-26 · agent

<!-- relay:entry 20260926T022531Z-q1 author=agent kind=event mention=1E5F model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
mentioned in #1E5F · 2026-09-26 · agent

<!-- relay:entry 20260926T022547Z-m4 author=agent kind=event model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
- ✦ agent moved this card · Discussing → Planned · Owner approved the recommendations and added first-run skill import with ongoing semantic catalog maintenance; scoped delivery cards filed as children.

<!-- relay:entry 20260926T022642Z-wv author=agent kind=event model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/b4029a35c63a45ab93a17253480c4f75 -->
- ✦ agent updated this card · replaced `## Discussion points`

<!-- relay:entry 20260926T034517Z-79 author=agent kind=event model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/1e8e402f6aeb4ed899e4e4be6dc00f5a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260926T034518Z-30 author=agent kind=event mention=1QKM model=gpt-6-sol pane=2914f43d turn=fc4b907796bb418994c523cffca15623/1e8e402f6aeb4ed899e4e4be6dc00f5a -->
mentioned in #1QKM · 2026-09-26 · agent
