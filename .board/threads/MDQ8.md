<!-- relay:entry 20260926T140758Z-3t author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · .board/changes/2026-09-26-on-restart-check-for-the-bug-where-a-pane-conten.md

<!-- relay:entry 20260926T142625Z-jz author=agent kind=event model=k3 pane=d773a199 turn=a4ceb8d1b45d4eb6b9ed8856eb2c734c/e76de0a35c6749c598967439d9708169 -->
- ✦ agent claimed this card · assignee agent, Inbox → Running, implemented_by kimi/k3, session d773a199

<!-- relay:entry 20260926T142625Z-k0 author=agent kind=progress model=k3 pane=d773a199 turn=a4ceb8d1b45d4eb6b9ed8856eb2c734c/e76de0a35c6749c598967439d9708169 pane_token=d773a199-4070-4356-9f2e-9b80b1734733 -->
Claimed (d773a199) · working on it from a terminal pane

Tracing why a restored pane shows no content until the user types a command, then fixing it.

<!-- relay:entry 20260926T145817Z-rm author=landq kind=note -->
Landing job c00795c7d24e063d (dd94c2d3829a) landed.
Published 412c772dc77c onto main (target was c8489f9208db). <!-- landq:c00795c7d24e063d:landed -->

<!-- relay:entry 20260926T150443Z-na author=agent kind=event model=claude-opus-5-5 pane=d773a199 turn=a4ceb8d1b45d4eb6b9ed8856eb2c734c/2219304906274efda1fa9ae5a49cfd8d -->
- ✦ agent moved this card · Running → Needs verification, wrote `## Done means`, `## Execution Summary`, `## Tests` · Root cause found and fixed with a regression test; commit e957f1f0 submitted to the landing queue (request mdq8-restore-replay-1). · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260926T150755Z-fy author=agent kind=progress model=claude-opus-5-5 pane=d773a199 turn=a4ceb8d1b45d4eb6b9ed8856eb2c734c/4670010bbbf548cdb3778c1b0e8115d7 -->
Second route found and fixed (commit da1f948c, landing job 711237606aecfebe). If a shell takes more than 5 s to send its first event under restart load, the integration watchdog put the pane in native mode without `m_autoHuman`. Nothing then left native mode at the first `ready`, so the pane had no prompt box and its restored text never printed. The watchdog now sets `m_autoHuman = true`, as `takeControl()` does before a first prompt. The `restorereplay` test gained `aSlowShellStillReplaysAfterTheWatchdog` (first `ready` delayed 6 s): it fails without the change and passes with it (3/3 on repeat).

<!-- relay:entry 20260926T151515Z-4p author=landq kind=note -->
Landing job 48a7415455fc7ee7 (e957f1f01b37 for card #MDQ8) was cancelled.
Nothing was published. <!-- landq:48a7415455fc7ee7:cancelled -->

<!-- relay:entry 20260926T151516Z-vw author=agent kind=note model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/337dca43c3394b20b9e6c48f7f9cfdfb -->
Landing job 48a74154 (e957f1f0) was cancelled at the owner's request to merge queued work: job 71123760 (da1f948c) from the same workspace contains e957f1f0, so it lands both commits in one gate. Nothing to resubmit.

<!-- relay:entry 20260926T151949Z-1j author=landq kind=note -->
Landing job fd4bd267d12fb6a3 (135d6d839e3c) landed.
Published 135d6d839e3c onto main (target was 221997442edc). <!-- landq:fd4bd267d12fb6a3:landed -->

<!-- relay:entry 20260926T151953Z-0t author=landq kind=note -->
Landing job 68276021e3e32a49 (f7c23e1e8d86) landed.
Published efbae72291a1 onto main (target was b67f3088ab8e). <!-- landq:68276021e3e32a49:landed -->

<!-- relay:entry 20260926T151958Z-g3 author=landq kind=note -->
Landing job 9b841c9c3a6dd855 (362fd71442c9) landed.
Published 722f2494270d onto main (target was c2252942b939). <!-- landq:9b841c9c3a6dd855:landed -->

<!-- relay:entry 20260926T153041Z-nt author=landq kind=note -->
Landing job 711237606aecfebe (da1f948c28a6 for card #MDQ8) landed.
Published 32be56adec21 onto main (target was f85a899e46d3).
Your workspace wt4437ca3982e51b88 may now be released; its receipt is `relay-land receipt 711237606aecfebe`. <!-- landq:711237606aecfebe:landed -->

<!-- relay:entry 20260926T153047Z-yd author=agent kind=progress model=claude-opus-5-5 pane=d773a199 turn=a4ceb8d1b45d4eb6b9ed8856eb2c734c/9a2bf4a246054edda0fa0ad6d60060b8 -->
Landed: job 711237606aecfebe published 32be56adec21 onto main. It carries both fixes, e957f1f0 (the shell poll retries a pending replay) and da1f948c (the watchdog's native mode ends at the first prompt). The earlier job 48a7415455fc7ee7 for e957f1f0 alone was cancelled by request; its commit landed inside this one. Still open for verification: a live restart with several restored panes, checking that each shows its text without a typed command.
