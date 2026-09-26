<!-- relay:entry 20260926T140036Z-r7 author=agent kind=event model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/5369bf3336354682b166859ecefc8c45 -->
- ✦ agent created this card in Planned · .board/features/2026-09-26-phone-tap-a-card-code-in-a-pane-to-open-it-long.md

<!-- relay:entry 20260926T140038Z-3d author=agent kind=event model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/5369bf3336354682b166859ecefc8c45 -->
- ✦ agent claimed this card · assignee agent, Planned → Running, implemented_by anthropic/claude-opus-5-5 via claude:ashe-ethz-ch, session 47171b5e

<!-- relay:entry 20260926T140038Z-a9 author=agent kind=progress model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/5369bf3336354682b166859ecefc8c45 pane_token=47171b5e-f82e-4bcd-a055-bf22f59d39e7 -->
Claimed (47171b5e) · working on it from a terminal pane

screen.js links well-formed card codes; app.js: tap → board.open (Back returns to the pane), long press → a small menu. Build and commit only; owner said do not submit.

<!-- relay:entry 20260926T140045Z-fk author=agent kind=event model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/5369bf3336354682b166859ecefc8c45 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["ai-visual", "person"], "huma…; replaced `## Done means`

<!-- relay:entry 20260926T140604Z-s4 author=agent kind=progress model=claude-opus-5-5 pane=47171b5e turn=126f58f09e8e410e9141b406f427a4e8/5369bf3336354682b166859ecefc8c45 -->
Implemented and committed in 71e8ee6e. Not submitted: the owner said "build and commit, but dont merge". Evidence is in docs/qa_evidence/2026-09-26-phone-card-codes/.
- Tap opens the card, and Back returns to the pane.
- Long press opens a sheet with Insert in prompt, Open card, Copy and Cancel; Insert fills the write sheet with the code at the cursor.
- A drag is a scroll and opens nothing.
- #1234 stays plain unless the board knows it.
172 phone-app tests pass and scripts/relay-build is clean.
