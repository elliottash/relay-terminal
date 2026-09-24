<!-- relay:entry 20260922T123500Z-a1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 12:35
Filed under policy rule 6 while running #1MGS's targeted tests; not fixed there because `agent.py`
is not that card's file. Reproduce: `python3 -m unittest tests.test_system_prompt`.

<!-- relay:entry 20260923T180237Z-vg author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, session fe15f16b

<!-- relay:entry 20260923T180237Z-vh author=agent kind=progress model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae pane_token=fe15f16b-a316-46dd-8841-78a4e061687d -->
Claimed (fe15f16b) · working on it from a terminal pane

Split the five-sentence terminal-evidence line in SYSTEM (agent.py:375) to one sentence per line; prove with tests.test_system_prompt.

<!-- relay:entry 20260923T180328Z-bj author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260923T180413Z-n6 author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["PLDG"], "github": null… → {"commits": ["da8d7ff8a94747444b122322dcbbf448fbf4fe50"], "evidence": [], "plans…; appended to `## Execution Summary`

<!-- relay:entry 20260923T180419Z-sy author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T180425Z-z7 author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent moved this card · unchanged · One-line prompt-formatting fix proved by its own test: tests.test_system_prompt 18/18 OK after failing at HEAD; landed in da8d7ff8. · implemented_by glm/glm-5.3

<!-- relay:entry 20260923T180431Z-72 author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent moved this card · Executing → Needs verification · Fix landed in da8d7ff8 and proved by tests.test_system_prompt (18/18 OK after failing at HEAD); small wording fix, no UI. · implemented_by glm/glm-5.3

<!-- relay:entry 20260923T180441Z-6e author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/41d05ce143574f9ca48f4b986bbb72ae -->
- ✦ agent moved this card · Needs verification → Done, session fe15f16b released · Targeted fix, verified by its own regression test (tests.test_system_prompt 18/18) and commit da8d7ff8; no UI, no open questions — closing as done. · verified_by glm/glm-5.3
