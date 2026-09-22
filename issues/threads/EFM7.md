<!-- relay:entry 20260922T180000Z-e7 author=codex kind=progress -->
### Codex · 2026-09-22 18:00
Created and claimed the request. Tracing the first configure response and adding a worker regression before changing effort reporting.

<!-- relay:entry 20260922T235420Z-32 author=agent kind=evidence model=gpt-6-astra pane=a95d8e80 turn=cbef684dc96b4134aab8544f399ada24/66640360c94643b080b8ca3c8ce1fae6 -->
Worker regression reproduces the report: configure starts FakeHarness at medium and reports guest_effort=medium, but generic effort=high. xhigh and ultra similarly report max. Fix makes configured/model_changed publish the harness level in both fields. Targeted guest-provider and session-protocol tests are running.

<!-- relay:entry 20260922T235508Z-g4 author=agent kind=event model=gpt-6-astra pane=a95d8e80 turn=cbef684dc96b4134aab8544f399ada24/66640360c94643b080b8ca3c8ce1fae6 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T235535Z-1d author=agent kind=event model=gpt-6-astra pane=a95d8e80 turn=cbef684dc96b4134aab8544f399ada24/66640360c94643b080b8ca3c8ce1fae6 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T235557Z-7w author=agent kind=event model=gpt-6-astra pane=a95d8e80 turn=cbef684dc96b4134aab8544f399ada24/66640360c94643b080b8ca3c8ce1fae6 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T235558Z-sx author=agent kind=event model=gpt-6-astra pane=a95d8e80 turn=cbef684dc96b4134aab8544f399ada24/66640360c94643b080b8ca3c8ce1fae6 -->
- ✦ agent moved this card · Executing → Needs verification · 126 targeted tests pass; medium/xhigh/ultra startup regression reproduced before the fix and passes after. Ready for independent GUI verification. · evidence docs/qa_evidence/2026-09-22-EFM7/results.txt · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T235613Z-7p author=agent kind=event model=gpt-6-astra pane=a95d8e80 turn=cbef684dc96b4134aab8544f399ada24/66640360c94643b080b8ca3c8ce1fae6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-EFM7/resu… → {"plans": [], "commits": ["c1e6ea7154accc193a00c7f915b44cf08522245e"], "evidence…
