<!-- relay:entry 20260921T232043Z-yh author=agent kind=progress model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
Claimed via file fallback as codex. Fixing missing tabs when opening the pane and when agents arrive later; keeping selection, focus, and explicit closes intact.

<!-- relay:entry 20260921T232200Z-vr author=agent kind=event model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260921T232201Z-2c author=agent kind=event model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260921T232201Z-fk author=agent kind=event model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260921T232237Z-6z author=agent kind=evidence model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
30 Qt subagent tests pass, including initial two-agent population, later arrival without focus/draft loss, independent view selection, one subscription per tab, explicit-close retention, reopening, and reused ids. Live Xvfb run confirms two visible tabs; tests_check has no failures or blockers.

<!-- relay:entry 20260921T232238Z-hr author=agent kind=event model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
- ✦ agent moved this card · Executing → Needs verification · All listed agents get tabs; 30 Qt tests and isolated Xvfb two-tab check pass. · evidence docs/qa_evidence/2026-09-21-subagent-tabs/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260921T232400Z-ye author=agent kind=event model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-21-subagent-… → {"plans": [], "commits": ["05a6ac725852f130fa24233e0cae632366442c1f"], "evidence…

<!-- relay:entry 20260921T232400Z-yf author=agent kind=evidence model=gpt-6-astra pane=2dd3cc26 turn=879618916dd84fbdbc1eb09a7c0886cb/178b82fca0f94df28acb014f8917fd51 -->
Landed 05a6ac72 after isolated commit-tree subagents build/test. Full shared Relay executable also rebuilt successfully (2026-09-21.19H.09). Screenshot reviewed with both tabs visible under the application theme.
