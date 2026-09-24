<!-- relay:entry 20260922T133440Z-a6 author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/d857a58b535541afbfc52244b2907c95 -->
- ✦ agent created this card in Discussing · issues/changes/2026-09-22-models-priorities-tab-rank-change-appears-to-do.md

<!-- relay:entry 20260922T133501Z-s1 author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/d857a58b535541afbfc52244b2907c95 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Discussion points`

<!-- relay:entry 20260922T133519Z-4n author=agent kind=question model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/d857a58b535541afbfc52244b2907c95 -->
What exactly did you try? On the current build both Alt+↑↓ and dragging a row within one section reorder and persist (measured under Xvfb, evidence in docs/qa_evidence/2026-09-22-rank-reorder-repro/). The ways it legitimately does nothing: dragging a row across a section header into another class, or dragging while the filter hides part of that list — both refuse silently, with the drop indicator suggesting it will work. Recommended fix: make a cross-section drag actually move the model between the two lists (remove + insert at the dropped rank, one undo step), and when a drop can't be stored (filtered view), say so in the limits line instead of snapping back quietly. If you meant something else — e.g. editing the rank number directly, or a drag that never started — say which and I'll chase that instead.

<!-- relay:entry 20260922T150346Z-r4 author=agent kind=decision model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
The owner answered the question: "ok, i think the issue was, the UI wasnt that responsive" — and asked for the fix on #RKP3: "add up / down buttons at the left in addition to alt up/down and dragging. and yes allow cross section dragging, and allow adding into each section." Landed there: cross-section drags now move the model (the silent snap-back is gone), and a drop that can't be stored says why on the limits line.

<!-- relay:entry 20260922T150501Z-j7 author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent moved this card · Discussing → Needs verification · Root cause found (silent no-ops on cross-section and filtered drops) and fixed with #RKP3: crossing drags now move the model, unstoreable drops say why, and ▲▼ buttons make rank changes discoverable. · evidence docs/qa_evidence/2026-09-22-rank-reorder-repro/ · implemented_by kimi/kimi-k3

<!-- relay:entry 20260922T150849Z-vq author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-rank-reor… → {"commits": ["02c32b7813fcc5f0dceea730bab65d23834fe6ee"], "evidence": ["docs/qa_…
