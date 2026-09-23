<!-- relay:entry 20260923T015742Z-b1 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:57
filed from #MCP7: relay-models.py check reports these three on main; left for the owner because it changes which Gemini model a Gemini pane starts on.

<!-- relay:entry 20260923T182552Z-t3 author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/e761a2eba201444288903a52f312ead4 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, session fe15f16b

<!-- relay:entry 20260923T182552Z-t4 author=agent kind=progress model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/e761a2eba201444288903a52f312ead4 pane_token=fe15f16b-a316-46dd-8841-78a4e061687d -->
Claimed (fe15f16b) · working on it from a terminal pane

Owner chose 'Follow the file': point TIER_DEFAULTS at model-ranking.md's picks and drop the three KNOWN_DRIFT lines; prove with relay-models.py check + the suite.

<!-- relay:entry 20260923T182600Z-da author=agent kind=decision model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/e761a2eba201444288903a52f312ead4 -->
"Follow the file" — owner's answer to the ask of 2026-09-23: TIER_DEFAULTS points at model-ranking.md's picks (gemini main=gemini-flash-latest flash=gemini-flash-latest; openrouter main=z-ai/glm-5.3-flash), accepting that a Gemini pane's main default drops from gemini-3.1-pro-preview to the gemini-flash-latest alias and OpenRouter main becomes z-ai/glm-5.3-flash.

<!-- relay:entry 20260923T183225Z-cv author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/e761a2eba201444288903a52f312ead4 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-MCP7/chec… → {"commits": ["b759f23deb4f09095716f4fb5a36d39b9f50b3f2"], "evidence": ["docs/qa_…

<!-- relay:entry 20260923T183233Z-z0 author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/e761a2eba201444288903a52f312ead4 -->
- ✦ agent moved this card · Executing → Done, session fe15f16b released · Owner's recorded decision ("follow the file") applied and proved: relay-models.py check → ok (exit 0), the drift suite green, the four named modules at 154 tests with only pre-existing relay-pro failures; landed in b759f23d. · verified_by glm/glm-5.3
