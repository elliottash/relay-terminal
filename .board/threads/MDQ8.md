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
