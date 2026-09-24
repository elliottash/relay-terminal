<!-- relay:entry 20260921T210000Z-c1 author=codex kind=progress -->
### Codex · 2026-09-21 21:00
Created and claimed this investigation. Tracing terminal fold layout, composer busy-row geometry, and queue-strip sizing; no source changes yet.

<!-- relay:entry 20260921T210000Z-c2 author=agent kind=question model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/e8720f5e83474b899e928c5583853f75 -->
1. Was the black space inside the terminal below its last output, or a separate blank band between the terminal and the prompt box? About how many lines tall was it? Recommendation: identify the affected surface before changing layout; thinking events alone do not establish which geometry was visible.

<!-- relay:entry 20260921T210000Z-c3 author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/e8720f5e83474b899e928c5583853f75 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260921T210000Z-c4 author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/e8720f5e83474b899e928c5583853f75 -->
- ✦ agent moved this card · Executing → Discussing · Clarifying whether the blank area was terminal viewport space or a separate layout band before making a UI change.

<!-- relay:entry 20260921T210000Z-c5 author=agent kind=progress model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/e8720f5e83474b899e928c5583853f75 -->
Read-only findings: Pane::thinkingDelta creates an inline fold only after thinking text arrives. PaneBusyLine is shown by tickTurnClock even during silent periods, but is intended to draw a single labeled row. The queue strip is only requested for entries, paused queues, or steers. No cause established yet; no source changes or claim of reproduction.

<!-- relay:entry 20260921T210000Z-c6 author=agent kind=decision model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
Owner clarified: "inside the terminal, below the output, between the terminal and the prompt box, like 7 lines". Investigate terminal viewport/fold layout with approximately seven trailing blank rows.

<!-- relay:entry 20260921T210000Z-c7 author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent updated this card · waiting_on: owner → (unset); appended to `## Decisions`

<!-- relay:entry 20260921T210000Z-c8 author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent moved this card · Discussing → Executing · Owner identified roughly seven blank rows within the terminal above the prompt; resume viewport investigation. · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260921T210000Z-c9 author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260921T210000Z-ca author=agent kind=progress model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
Reproduced missing lower terminal rows with compressed prose followed by ordinary output. The renderer previously supplied only the first core frame. Fetching the remaining visible rows restores the output; targeted tests for fold opening/closing, scrolling, resize, and prose reflow pass. Extending coverage to link hit testing and drag selection before landing.

<!-- relay:entry 20260921T210000Z-cb author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260921T210000Z-cc author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260921T210000Z-cd author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260921T210000Z-ce author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent moved this card · Executing → Needs verification · Matching blank-row fault reproduced and fixed; seven focused engine GUI tests pass under Xvfb/libvterm. Original Codex scenario awaits visual confirmation. · evidence docs/qa_evidence/2026-09-21-terminal-bottom-gap/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260921T210000Z-cf author=agent kind=event model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-21-terminal-… → {"plans": [], "commits": ["a5e1613724fb08354fe1a71ee5992c8a790fc5f6"], "evidence…; replaced `## Tests`

<!-- relay:entry 20260921T210000Z-cg author=agent kind=evidence model=gpt-6-astra pane=7281bb88 turn=d51b78b277a64a1b92bf122370d5cf42/581fe9d5e7404af1aaf8c09fbb567e80 -->
Landed a5e1613724fb on main. The exact committed tree built via land.py and all seven focused ViewTest functions passed on that tree (9 passes including setup/cleanup). Xvfb run and screenshot also passed. tests_check now reports no findings, failing tests, or blocking signals for B7SP. Board format check has no diagnostics on B7SP/W7DC; three pre-existing errors remain elsewhere.
