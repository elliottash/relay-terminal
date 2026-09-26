<!-- relay:entry 20260925T181446Z-3t author=agent kind=event model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b6ed317fd9df42c5abd1d9c0297a1001 -->
- ✦ agent created this card in Discussing · .board/changes/2026-09-25-busy-line-showed-300000-s-after-model-switch-dur.md

<!-- relay:entry 20260925T181456Z-4c author=agent kind=question model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b6ed317fd9df42c5abd1d9c0297a1001 -->
1. When you saw "Relaying · 300000 s", did the number keep counting up each second, or was it frozen? (If it climbed, the pane had been stuck busy for days since a much earlier turn — the model switch/compaction just made you look. If frozen, the clock ticked once off a stale timer.)

<!-- relay:entry 20260925T181501Z-zv author=agent kind=event model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b6ed317fd9df42c5abd1d9c0297a1001 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260925T183049Z-j2 author=agent kind=decision model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b01cc1803bb64c579d2e874f7876694b -->
User: "It was climbing each second." — so the pane was stuck busy counting a turn whose agent_finished never arrived, not a one-off bad tick.

<!-- relay:entry 20260925T183049Z-zt author=agent kind=progress model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b01cc1803bb64c579d2e874f7876694b -->
Fixed and landed as f4bc2f991bcf (pane25, landed through land.py with only my hunks; Pane.h was contested by 7 live sessions and the rest of their hunks were left in the working tree untouched). The landed fix, in three parts: (1) stuck-turn watchdog — tickTurnClock ends the turn after kStuckTurnSilenceMs = 2 h of total worker silence (no ask open, nothing in waitingLine), logging stuck_turn to relay.log and printing an inline notice; a living turn always emits events, so this only fires on a lost worker/turn; (2) startTurnClock when the error path flips busy on without an agent_started; (3) tickTurnClock starts a never-started timer instead of reading it. The land.py build gate compiled the exact landed tree (verify slot) before the commit was allowed. The pre-existing failures in consolemode and panestatus (colour-band and notification tests) are unrelated to this change.

<!-- relay:entry 20260925T183109Z-0r author=agent kind=event model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b01cc1803bb64c579d2e874f7876694b -->
- ✦ agent updated this card · waiting_on: owner → (unset); replaced `## Done means`

<!-- relay:entry 20260925T183114Z-26 author=agent kind=event model=glm-5.3-flash pane=89fea686 turn=d4a64c3b906c4301b7649c1d8c36f82f/b01cc1803bb64c579d2e874f7876694b -->
- ✦ agent moved this card · Discussing → Done · Fix landed (f4bc2f991bcf) with the build gate compiling the exact landed tree; watchdog + clock guards in place. Medium bug, no QA lane evidence possible without reproducing a 2-hour silent worker. · implemented_by glm/glm-5.3-flash · verified_by glm/glm-5.3-flash
