<!-- relay:entry 20260925T010958Z-7z author=agent kind=event model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a -->
- ✦ agent created this card in Planned · .board/features/2026-09-24-pause-a-subagent-esc-in-its-pane-and-a-pause-but.md

<!-- relay:entry 20260925T011001Z-cj author=agent kind=event model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, session d067d65d

<!-- relay:entry 20260925T011001Z-zj author=agent kind=progress model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a pane_token=d067d65d-0caa-494c-82fd-0f973e068a42 -->
Claimed (d067d65d) · working on it from a terminal pane

Implementing: backend agent_pause/agent_resume with a "paused" status; Esc in the subagent tabs pane pauses the live front tab; the strip gets a pause/resume button beside the x.

<!-- relay:entry 20260925T011056Z-11 author=agent kind=event model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "system", "primary": "script", "also": [], "human": "none", "criter…; appended to `## Done means`

<!-- relay:entry 20260925T013215Z-sh author=agent kind=event model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260925T013221Z-sv author=agent kind=event model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T013224Z-jx author=agent kind=event model=glm-5.3 pane=d067d65d turn=7b4f4839ac9047c3b03c69e4000d740a/cb4b5c19ac564933bbb91b1313863e6a -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and committed (49dd6a6): agent_pause/agent_resume backend, Esc-in-pane hold, ‖/▶ strip buttons; tests green, evidence shots captured. · evidence docs/qa_evidence/2026-09-24-subagent-pause/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T134807Z-dq author=agent kind=evidence model=glm-5.3 pane=02674f5a turn=5eb7af1a47ca4ab3b7edc202ebb806a7/d393423c789c476b88de18c24b6203c7 -->
Manual-run check found a real fault: the pause/resume marks were text glyphs (‖ U+2016, ▶ U+25B6) and the UI font (Noto Sans) draws ‖ as a hairline and has no ▶ at all, so the strip button and the header button fell back to foreign glyphs — the user's screenshot shows the blob. Fixed in 8559e15 (session elliott-pause-glyph): both now paint two bars / a filled triangle with QPainter (same hit rect and p key on the strip; painted QIcon + "pause"/"resume" text beside it on the header). statusIcon("paused") stays the ‖ string in tab text. Tests: subagents (new assertion: the header button carries a painted icon) and striplayout green; verify-slot build green. The manual-run item of the criteria should be re-checked against this build.
