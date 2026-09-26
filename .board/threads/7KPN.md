<!-- relay:entry 20260923T122311Z-a1 author=codex kind=progress -->
### Codex · 2026-09-23 12:23
Claimed this request. The picker currently uses preset labels and notes that contain model names; changing it to provider names while preserving plan choices.

<!-- relay:entry 20260923T122733Z-5w author=agent kind=event model=gpt-6-sol pane=b91c71cd turn=19bd73c570494541b8433b0c939c9daf/64fe62ff82f14e71b8275888ebaa081a -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260923T122738Z-vm author=agent kind=event model=gpt-6-sol pane=b91c71cd turn=19bd73c570494541b8433b0c939c9daf/64fe62ff82f14e71b8275888ebaa081a -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T123010Z-6n author=agent kind=event model=gpt-6-sol pane=b91c71cd turn=19bd73c570494541b8433b0c939c9daf/64fe62ff82f14e71b8275888ebaa081a -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["4BPE"], "github": null… → {"plans": [], "commits": ["07cb34a30b23a3e50da9f96212139a75575351ec", "8d9f24cf3…; appended to `## Tests`

<!-- relay:entry 20260923T123014Z-jr author=agent kind=event model=gpt-6-sol pane=b91c71cd turn=19bd73c570494541b8433b0c939c9daf/64fe62ff82f14e71b8275888ebaa081a -->
- ✦ agent moved this card · Executing → Needs verification · Provider picker and key prompt now use company names; clean build and isolated UI captures passed. · evidence docs/qa_evidence/2026-09-23-provider-names/ · implemented_by openai/gpt-6-sol via codex
<!-- relay:entry 20260925T223533Z-9o author=agent kind=note -->
Verification (rev 2db96643): PASSED — picker lists provider names only, key prompt says "Key for kimi", row summary names providers; code survives at HEAD in RelayWindowModels.cpp:460-487 after #243T's move. Evidence: docs/qa_evidence/2026-09-25-verify-7KPN/. Moved needs-verification → needs-qa-llm.

