<!-- relay:entry 20260919T202200Z-j7 author=agent kind=event model=glm-5.3 pane=3791b21e turn=7935ced5c8a54973b71b071747972a2a/b8d1354760cf4017ae23672f44ba2abf -->
- ✦ agent created this card in Inbox · issues/features/2026-09-19-switchboard-filter-bar-full-text-search.md

<!-- relay:entry 20260919T202205Z-n9 author=agent kind=event model=glm-5.3 pane=3791b21e turn=7935ced5c8a54973b71b071747972a2a/b8d1354760cf4017ae23672f44ba2abf -->
- ✦ agent moved this card · Inbox → In progress · Starting work: the worker will carry each card's body and thread text on its row so the pane's filter can search it. · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T211927Z-rs author=agent kind=event model=glm-5.3 pane=dca8ae5e turn=7935ced5c8a54973b71b071747972a2a/feff85a793114699ada45cc8d521d957 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T211932Z-p8 author=agent kind=event model=glm-5.3 pane=dca8ae5e turn=7935ced5c8a54973b71b071747972a2a/feff85a793114699ada45cc8d521d957 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented: rows carry the card's body and thread as `text` (capped 64 KiB), the pane's plain filter words match it, tests and live Xvfb evidence recorded. · evidence docs/qa_evidence/2026-09-19-board-filter-full-text/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T212518Z-8g author=agent kind=event model=glm-5.3 pane=dca8ae5e turn=7935ced5c8a54973b71b071747972a2a/feff85a793114699ada45cc8d521d957 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-19-board-fil… → {"plans": [], "commits": ["9973dcbf"], "evidence": ["docs/qa_evidence/2026-09-19…

<!-- relay:entry 20260919T212518Z-k7 author=agent kind=progress model=glm-5.3 pane=dca8ae5e turn=7935ced5c8a54973b71b071747972a2a/feff85a793114699ada45cc8d521d957 -->
Landed as 9973dcbf on main (worker's qa.commits already shows it): worker rows carry `text` (body + thread, 64 KiB cap), Model::matches searches it, board_list rows stay light, protocol 19.2 documents the field. Two cosmetic bits are in the working tree but not in that commit because two other live sessions hold those files (9k5h-thinking's Sort menu and k9x6-esc-filter's edits are interleaved in the same hunks): the filter box placeholder ("any word in the card…", src/BoardPane.cpp) and the full-text sentence in docs/SWITCHBOARD-DESIGN.md's Filtering paragraph. Both are inside those sessions' snapshots and land with their commits; the feature itself does not depend on them.
