<!-- relay:entry 20260925T021916Z-3a author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent created this card in Planned · .board/features/2026-09-24-card-links-open-the-board-on-the-card-alone-not.md

<!-- relay:entry 20260925T021920Z-s1 author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session 7d4ada28

<!-- relay:entry 20260925T021920Z-s2 author=agent kind=progress model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b pane_token=7d4ada28-5968-4825-9a61-1e270948311b -->
Claimed (7d4ada28) · working on it from a terminal pane

Make RelayWindow's card-link reveals open the card full-pane (solo): updateDetailLayout treats the pane as stacked while that page is open; add openCardSolo to BoardView, a test, and a screenshot.

<!-- relay:entry 20260925T021927Z-81 author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent updated this card · verify: {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off… → {"artifact": "code", "primary": "script", "also": ["ai-visual"], "human": "none"…; appended to `## Done means`

<!-- relay:entry 20260925T022652Z-gz author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260925T022657Z-pn author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T022700Z-xp author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in e0d642c8df0c, builds and boardpane tests pass; UI change for a separate verifier. · evidence docs/qa_evidence/2026-09-24-card-link-solo/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T022703Z-qz author=agent kind=progress model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T023801Z-ph author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent updated this card · appended to `## Try it`

<!-- relay:entry 20260925T023807Z-es author=agent kind=evidence model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
Try-it staged at docs/qa_evidence/2026-09-24-tryit-K4SQ/: stage.sh (re-runnable; Xvfb :187 2200x1200, sandboxed HOME, RELAY_KEYRING=off, no model, no network), steps.md (check 1 / agent 1 / person 1), expected.md sealed, shots 02/03/04. Agent pass through named controls only (relay-drive open AA01 = the window's openBoardCard; read boardKeys: "Esc back to the board…" solo vs "Enter open…" list; press boardCardClose); no pointer input, no code changed to stage.

<!-- relay:entry 20260925T023827Z-8y author=agent kind=event model=glm-5.3 pane=7d4ada28 turn=29a8f1fd84ec47ada945ff8ec31d51de/a6dda6071846475e9bbd15d9689bcf8b -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-24-card-link… → {"commits": ["e0d642c8", "4bd0884"], "evidence": ["docs/qa_evidence/2026-09-24-c…
