<!-- relay:entry 20260923T161000Z-r4 author=codex kind=progress -->
### Codex · 2026-09-23 16:10
Claimed this request. Tracing the request count and header sorting before editing the Sessions list.

<!-- relay:entry 20260923T161444Z-sj author=agent kind=progress model=gpt-6-sol pane=7d5859de turn=d07fe423cc0541e0ac86ecc83c13996d/37e9dd35b60049fe9a91a78f85ddcd68 -->
Confirmed the count is RequestLedger.open_count(): user requests still open or in progress. Qt's header was not set clickable, while existing tests emitted sectionClicked directly; this explains why the dropdown worked but mouse clicks did not. Added a Requests column and enabled real header clicks; now updating sort IDs and tests.

<!-- relay:entry 20260923T162033Z-wk author=agent kind=event model=gpt-6-sol pane=7d5859de turn=d07fe423cc0541e0ac86ecc83c13996d/37e9dd35b60049fe9a91a78f85ddcd68 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["EV45"], "github": null… → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-23-open-requ…; appended to `## Execution Summary`

<!-- relay:entry 20260923T162040Z-ge author=agent kind=event model=gpt-6-sol pane=7d5859de turn=d07fe423cc0541e0ac86ecc83c13996d/37e9dd35b60049fe9a91a78f85ddcd68 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T162227Z-ms author=agent kind=event model=gpt-6-sol pane=7d5859de turn=d07fe423cc0541e0ac86ecc83c13996d/37e9dd35b60049fe9a91a78f85ddcd68 -->
- ✦ agent moved this card · Executing → Needs verification · Requests column, request-count sorting and real header clicks pass targeted backend and Qt tests; screenshot captured. · evidence docs/qa_evidence/2026-09-23-open-requests-R4WX/ · implemented_by openai/gpt-6-sol via codex
