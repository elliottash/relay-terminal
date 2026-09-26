<!-- relay:entry 20260925T020219Z-zy author=agent kind=event model=glm-5.3 pane=53a5ae27 turn=9cb6e764d69b440bab5741ee388976e5/68da094de3c7481eb491c71d4a5813c1 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-24-panestatus-tests-fail-on-main-band-hues-from-mxm.md

<!-- relay:entry 20260925T052725Z-pv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T052725Z-pw author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T052940Z-h4 author=agent kind=event model=glm-5.3 pane=switchboard turn=67c7c26d3c856df7cc9955d5e63efc35/c6a07ae2715046728b0098fcfe4270a8 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T052956Z-dy author=agent kind=event model=glm-5.3 pane=switchboard turn=67c7c26d3c856df7cc9955d5e63efc35/c6a07ae2715046728b0098fcfe4270a8 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T053003Z-vt author=agent kind=comment mode=plan model=glm-5.3 turn=67c7c26d3c856df7cc9955d5e63efc35/c6a07ae2715046728b0098fcfe4270a8 -->
Planned #WMX7. The root cause is one line: #MXMG's +0.05 band notch for Board and Sessions (`src/PaneStatus.cpp:355`) applies in ByGroup mode too, where both panes wear the shared tools brass — splitting the group's fill into two strengths, which is exactly the `#ff27241e` vs `#ff302b22` and `byGroup.size()` 2 failures; the plan gates the notch to ByType, where the two panes actually have distinct hues, and leaves the tests untouched.

One owner question sits in Risks: if you wanted the stronger band in ByGroup as well, the fix flips to editing the tests' group invariant instead — the plan defaults to the code fix. Everything else needs nothing from you; press **Run** when ready.

<!-- relay:entry 20260925T053003Z-vu author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260926T003549Z-vm author=agent kind=event mention=3MH4 model=claude-fable-5-1 pane=f35051fe turn=dbc9af56def4427c91f4fecd2d277da4/1a6acee91a96495087b757d1ef3ee4e7 -->
mentioned in #3MH4 · 2026-09-26 · agent
