<!-- relay:entry 20260923T111900Z-p1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 11:19
Owner: "i like this terminology. write the plan on a card". Terminology accepted: Send, Plan, Run (replaces Execute), Run in background (replaces "Send & hide"), Move to background, Open, Verify; Deliver stays the /deliver workflow. The design discussion (Codex, pane 520ccb90, 2026-09-22 20:11–20:57) is recorded in `## Decisions`; its source is the Relay scrollback for that pane.

<!-- relay:entry 20260923T111901Z-p2 author=claude-code kind=plan -->
### Claude Code · 2026-09-23 11:19
Filed with `## Done means` and a nine-step `## Plan`. Two owner decisions are open under Risks: the key for Run (`r` recommended) and whether green done tasks clear after opening (recommended). Related: #RG0Z (dim while working).

<!-- relay:entry 20260923T111901Z-p3 author=agent kind=event model=claude-opus-5-5 pane=520ccb90 turn=a7740578e6674cdea34c8be534de20ad/549a77b534dd4af2b4da06393a9fad8c -->
- ✦ agent updated this card · appended to `## Decisions`; replaced `## Plan`

<!-- relay:entry 20260923T111901Z-p4 author=agent kind=decision model=claude-opus-5-5 pane=520ccb90 turn=a7740578e6674cdea34c8be534de20ad/549a77b534dd4af2b4da06393a9fad8c -->
Owner: "1 yes, and you can go ahead and remove x." Run moves to `r`, and `x` is removed with no alias. Owner: "2 yes." Green done tasks clear once opened. On the owner's question "for claude / codex and 'done'. how do relay models do it?": built-in agents use the request ledger plus the completion check, and guests already run through the same `Agent` loop. The only gap is that guests' native todo lists are not mirrored, so the plan now has a step to mirror them (step 7) and one done rule for both.

<!-- relay:entry 20260923T120107Z-zq author=agent kind=note model=claude-opus-5-5 pane=520ccb90 turn=a7740578e6674cdea34c8be534de20ad/7accc7d4bb9a487c85dbe5ad2627817f -->
Rewrote step 7 and its findings after checking the installed guests. Claude Code 2.1.280 uses TaskCreate/TaskGet/TaskUpdate/TaskList, not TodoWrite. They are off by default on current models, and `--disallowedTools` can guarantee that, so Relay's `update_todos` becomes Claude's only list. Codex 0.156.0 has no switch to disable `update_plan`, so the harness will mirror its `turn/plan/updated` notifications into Relay todos for the current request. Owner asked: "is there a way to force claude and codex to use relay tasks natively".

<!-- relay:entry 20260923T164414Z-59 author=agent kind=event model=gpt-6-sol pane=661abafc turn=a7740578e6674cdea34c8be534de20ad/f9132d0ee2db4c4a96f9e492e011bb07 -->
- ✦ agent moved this card · Planned → Executing · Implementing the approved Run in background plan in this pane. · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260923T164417Z-v2 author=agent kind=event model=gpt-6-sol pane=661abafc turn=a7740578e6674cdea34c8be534de20ad/f9132d0ee2db4c4a96f9e492e011bb07 -->
- ✦ agent updated this card · assignee: (unset) → codex

<!-- relay:entry 20260923T181553Z-xe author=agent kind=progress model=gpt-6-sol pane=6e4865f9 turn=a7740578e6674cdea34c8be534de20ad/36297f8d0a514ae29ba79e7494449a87 -->
Implemented the live-pane background path, Board Run default and Run in pane, request-linked state/counts/notifications, interrupted restore, Claude task-tool disable, and Codex plan mirroring. The main build and board/request tests are being checked; GUI evidence and landing remain.

<!-- relay:entry 20260923T182921Z-qy author=agent kind=event model=gpt-6-sol pane=6e4865f9 turn=a7740578e6674cdea34c8be534de20ad/36297f8d0a514ae29ba79e7494449a87 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["RG0Z"], "github": null… → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-23-bgrn/"], …; appended to `## Execution Summary`

<!-- relay:entry 20260923T182929Z-gr author=agent kind=event model=gpt-6-sol pane=6e4865f9 turn=a7740578e6674cdea34c8be534de20ad/36297f8d0a514ae29ba79e7494449a87 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T184918Z-6f author=agent kind=event model=gpt-6-sol pane=6e4865f9 turn=a7740578e6674cdea34c8be534de20ad/36297f8d0a514ae29ba79e7494449a87 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-23-bgrn/"], … → {"plans": [], "commits": ["854c097de2cf7d2903aa55738529185c63415f7e"], "evidence…

<!-- relay:entry 20260923T184923Z-2c author=agent kind=event model=gpt-6-sol pane=6e4865f9 turn=a7740578e6674cdea34c8be534de20ad/36297f8d0a514ae29ba79e7494449a87 -->
- ✦ agent moved this card · Executing → Needs verification · Implementation landed in 854c097d; isolated Board UI probe, request lifecycle tests, guest plan mirror tests, and exact-tree build are recorded. · evidence docs/qa_evidence/2026-09-23-bgrn/ · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260923T185053Z-2k author=agent kind=event model=gpt-6-sol pane=6e4865f9 turn=a7740578e6674cdea34c8be534de20ad/36297f8d0a514ae29ba79e7494449a87 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["854c097de2cf7d2903aa55738529185c63415f7e"], "evidence… → {"plans": [], "commits": ["854c097de2cf7d2903aa55738529185c63415f7e", "3ac63e272…
