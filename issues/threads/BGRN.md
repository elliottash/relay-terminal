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
