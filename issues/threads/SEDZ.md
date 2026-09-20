<!-- relay:entry 20260919T213200Z-x1 author=agent kind=event model=glm-5.3 pane=6399f229 turn=42e73bd9d9b44c25a6276e05b532d7a1/a510bc4f049d4062afa2c8aefc2dc785 -->
- ✦ agent created this card in Inbox · issues/features/2026-09-19-the-switchboard-list-can-be-sorted-especially-by.md

<!-- relay:entry 20260919T213202Z-wy author=agent kind=event model=glm-5.3 pane=6399f229 turn=42e73bd9d9b44c25a6276e05b532d7a1/a510bc4f049d4062afa2c8aefc2dc785 -->
- ✦ agent moved this card · Inbox → In progress · the agent picked it up in the relay-terminal pane this turn · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T213218Z-va author=agent kind=event model=glm-5.3 pane=6399f229 turn=42e73bd9d9b44c25a6276e05b532d7a1/a510bc4f049d4062afa2c8aefc2dc785 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260919T213225Z-6p author=agent kind=event model=glm-5.3 pane=6399f229 turn=42e73bd9d9b44c25a6276e05b532d7a1/a510bc4f049d4062afa2c8aefc2dc785 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T223601Z-nw author=agent kind=event model=glm-5.3 pane=cec9c04e turn=42e73bd9d9b44c25a6276e05b532d7a1/f09829265a7f4d5fa07165dcfb610009 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"github": null, "commits": ["eb6909ae"], "evidence": ["docs/qa_evidence/2026-09…

<!-- relay:entry 20260919T223604Z-tx author=agent kind=event model=glm-5.3 pane=cec9c04e turn=42e73bd9d9b44c25a6276e05b532d7a1/f09829265a7f4d5fa07165dcfb610009 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · implemented and landed (eb6909ae for the restore hunks; the sweep 3e12fa58/5e229706 carried the rest); unit tests and the live Xvfb/OCR run pass · evidence docs/qa_evidence/2026-09-19-switchboard-sort/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T000512Z-kk author=agent kind=note model=deepseek/deepseek-v4.1-flash pane=6b0791bf turn=0df24bada3fe47e38957819f62d2134d/c7dd1e01e34d464d8e9a42c65dee714d -->
The owner has changed this one's control, so the button this card landed is gone (superseding it, not a fault in it): the sort is now the list's own column header — Card, Created, Updated, each a click, with the two dates also drawn as the row's right-hand columns — filed as #Z2VT (`docs/qa_evidence/2026-09-19-switchboard-column-header/`). The orders this card defined are all still there (`manual`, `newest`, `oldest`, `updated`) and the layout node still saves the choice; #Z2VT adds `updated-oldest`, `title` and `title-desc` and the cycle back to Manual on a third click. A QA session on this card should read it as: the sort menu and the `boardSort` button no longer exist, and the refusal notice now points at the header.
