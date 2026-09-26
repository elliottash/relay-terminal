<!-- relay:entry 20260926T131014Z-v2 author=agent kind=event model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/1386ecb263de494d883dd71a78e40bc5 -->
- ✦ agent created this card in Planned · .board/changes/2026-09-26-phone-terminal-typed-row-bands-agent-prompt-shel.md

<!-- relay:entry 20260926T131019Z-9m author=agent kind=event model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/1386ecb263de494d883dd71a78e40bc5 -->
- ✦ agent claimed this card · assignee agent, Planned → Running, implemented_by anthropic/claude-opus-5-5 via claude:ashe-ethz-ch, session 47171b5e

<!-- relay:entry 20260926T131019Z-kt author=agent kind=progress model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/1386ecb263de494d883dd71a78e40bc5 pane_token=47171b5e-f82e-4bcd-a055-bf22f59d39e7 -->
Claimed (47171b5e) · working on it from a terminal pane

Keep `marks` through remote/gui_host.py and wire.apply_scroll; paint bands in app/screen.js from the web theme's shell/agent colours.

<!-- relay:entry 20260926T131026Z-81 author=agent kind=event model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/1386ecb263de494d883dd71a78e40bc5 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "visual", "primary": "script", "also": ["ai-visual"], "human": "opt…; replaced `## Done means`

<!-- relay:entry 20260926T133339Z-rc author=agent kind=event mention=53GR model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/1386ecb263de494d883dd71a78e40bc5 -->
mentioned in #53GR · 2026-09-26 · agent

<!-- relay:entry 20260926T133341Z-ac author=agent kind=progress model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/1386ecb263de494d883dd71a78e40bc5 -->
Implemented in e5c8e7ee: marks are kept through gui_host and apply_scroll, and screen.js draws the bands. In 4bc23e25 the desktop's "Band behind what you typed" setting now travels as pane_state.band, and the phone draws the chrome and none looks. Evidence is in docs/qa_evidence/2026-09-26-phone-typed-row-bands/. Queued with #8R3V and #53GR as job 6dec3d731e824cd8.
