<!-- relay:entry 20260922T143054Z-np author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent created this card in Planned · issues/features/2026-09-22-priorities-page-move-buttons-cross-section-drags.md

<!-- relay:entry 20260922T143103Z-gx author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by kimi/kimi-k3, session a352a041

<!-- relay:entry 20260922T143103Z-q5 author=agent kind=progress model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a pane_token=a352a041-d9f2-4234-9a03-49182f601b42 -->
Claimed (a352a041) · working on it from a terminal pane

Implementing: ▲▼ move buttons in a new left column, cross-section drag = move between lists (any section incl. empty), footer rewrite without alt+numbers. Tests + Xvfb evidence to follow.

<!-- relay:entry 20260922T143148Z-wr author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent updated this card · appended to `## Plan`; replaced `## Done means`; tasks: 0/6 done

<!-- relay:entry 20260922T150332Z-he author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent updated this card · appended to `## Execution Summary`; replaced `## Tests`; tasks: 6/6 done

<!-- relay:entry 20260922T150450Z-jk author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent moved this card · Executing → Needs verification · Implemented: ▲▼ buttons, cross-section drags stored as moves, honest refusal notice, footer without alt+digits. 51+19 unit tests pass; Xvfb drive persisted both new paths. · evidence docs/qa_evidence/2026-09-22-rank-reorder-repro/ · implemented_by kimi/kimi-k3

<!-- relay:entry 20260922T150849Z-a2 author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-rank-reor… → {"commits": ["02c32b7813fcc5f0dceea730bab65d23834fe6ee"], "evidence": ["docs/qa_…

<!-- relay:entry 20260922T150915Z-cf author=agent kind=progress model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260922T151237Z-ew author=agent kind=event model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
- ✦ agent updated this card · replaced `## Try it`

<!-- relay:entry 20260922T151237Z-jy author=agent kind=evidence model=kimi-k3 pane=a352a041 turn=ef7ab5c3ef21445ab80dea50fedfbb29/dc0d3272490b4c4ea245dc42cbb1048a -->
Try it staged in docs/qa_evidence/2026-09-22-tryit-RKP3/ (own staging; none was left by a verifying session). Ran stage.sh myself under Xvfb: Relay opened with the seeded high/main/flash sections, every listed row carrying ▲▼ buttons, footer reading "tab / shift+tab: the pane's tabs · ▲▼ or alt+↑↓ moves a row · drag to reorder — or into…" (open.png). The button click and the cross-section drag were already played end-to-end in this same fixture shape and asserted against the written config (docs/qa_evidence/2026-09-22-rank-reorder-repro/drive4.py): ▼ moved main m1→rank 2 (m2,m1,m3), and dragging m1 into high stored high=h1,h2,m1 and main=m2,m3. Path with check/agent/person marks in path.md.
