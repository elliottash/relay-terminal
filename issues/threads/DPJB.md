<!-- relay:entry 20260920T135027Z-ky author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=bc40207e turn=e69097f71a1943fbb93aadc1103be5a7/9b438a56ef764a0d93d8f875c5e7a287 -->
- ✦ agent created this card in Inbox · issues/features/2026-09-20-priority-flags-on-the-card-page-shift-them-by-cl.md

<!-- relay:entry 20260920T135610Z-cm author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=bc40207e turn=e69097f71a1943fbb93aadc1103be5a7/9b438a56ef764a0d93d8f875c5e7a287 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T142411Z-zs author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=bc40207e turn=e69097f71a1943fbb93aadc1103be5a7/9b438a56ef764a0d93d8f875c5e7a287 -->
- ✦ agent updated this card · appended to `## Evidence`

<!-- relay:entry 20260920T142422Z-6e author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=bc40207e turn=e69097f71a1943fbb93aadc1103be5a7/9b438a56ef764a0d93d8f875c5e7a287 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T142539Z-nc author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=bc40207e turn=e69097f71a1943fbb93aadc1103be5a7/9b438a56ef764a0d93d8f875c5e7a287 -->
- ✦ agent moved this card · Inbox → Needs verification · Implemented: the card page's header grows the row's priority flag (left/right click, clamped −1…+3, one write path with the row), agents are told the flag is settable through `board_update_card`, plus docs and tests. Evidence and QA checklist on the card; live Xvfb run in the evidence folder. · evidence docs/qa_evidence/2026-09-20-card-page-priority-flag/ · implemented_by deepseek/deepseek-v4.1-flash

<!-- relay:entry 20260921T115610Z-67 author=agent kind=progress -->
The #DPJB test was sitting uncommitted in the shared checkout. Landed unchanged on 2026-09-21 as `039ef82c` after land.py's build gate built the exact tree and `ctest -R '^boardpane$'` passed in it (owner: clean the tree). It is `theCardPagesFlagClicksThroughToBoardPriority` in `tests/boardpane_test.cpp` — the card page's flag clicked left and right, the optimistic tooltip, and the clamp at −1 — against the `PriorityFlagButton` code that was already on main.
