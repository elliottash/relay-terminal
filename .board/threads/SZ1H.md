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
