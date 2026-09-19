<!-- relay:entry 20260919T044257Z-tp author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent created this card in Inbox · issues/features/2026-09-19-retry-transient-provider-refusals-429-5xx-in-the.md

<!-- relay:entry 20260919T044305Z-wb author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent moved this card · Inbox → In progress · Starting work: transport-level retry in backend/relay_core/provider.py.

<!-- relay:entry 20260919T044616Z-kh author=owner kind=comment mode=discuss -->
for model retries, if a model doesnt work, try another provider after 3 tries or something

<!-- relay:entry 20260919T050210Z-q7 author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent updated this card · appended to `## Tasks`

<!-- relay:entry 20260919T050216Z-x5 author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T050216Z-x6 author=agent kind=note model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
Retries are in (transport-level, Claude Code's policy; see the QA checklist). "Try another provider after 3 tries" is agent-level failover over the roles/keystore machinery — filed as its own card so it can build on this one and stay reviewable.

<!-- relay:entry 20260919T050242Z-ws author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented and tested; QA checklist and evidence in the body. · evidence docs/qa_evidence/2026-09-19-provider-http-retries/
