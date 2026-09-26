<!-- relay:entry 20260925T222241Z-dr author=agent kind=event model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-consolemode-default-suite-ctrl-click-opens-the-e.md

<!-- relay:entry 20260925T223132Z-zw author=agent kind=evidence model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
Corroborating evidence from #DSKT's session (d1d07d15): a clean-tip scratch build of 134aa033 (git archive → fresh cmake, ccache) fails the same checks — consolemode_test.cpp 633/634/646 (edited != path, line != 2, context.seen) and 1917/1920 (thinking-rows spacing) — so the failures predate both #8ABD's and #DSKT's uncommitted work. The failing set moves between runs (720/721/733 + occasionally 2007/1917/1920 in the combined tree), i.e. flaky, not deterministic. Reproduce: build clean tip, `QT_QPA_PLATFORM=offscreen ./relay-consolemode-tests` — exit 1 with those FAILs.

<!-- relay:entry 20260925T223132Z-zx author=agent kind=event model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260925T224533Z-hr author=agent kind=event model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
- ✦ agent updated this card · appended to `## Evidence`; verify defaulted from skill deliver

<!-- relay:entry 20260925T225447Z-45 author=agent kind=event mention=8ABD model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
mentioned in #8ABD · 2026-09-25 · agent
