<!-- relay:entry 20260925T012033Z-cw author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/2f35f6ba247e4f2a9c3045b774427f0a -->
- ✦ agent created this card in Planned · .board/changes/2026-09-24-tests-find-their-child-processes-with-pgrep-and.md

<!-- relay:entry 20260925T012042Z-yv author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/2f35f6ba247e4f2a9c3045b774427f0a -->
- ✦ agent updated this card · replaced `## Planning notes`

<!-- relay:entry 20260925T040031Z-tc author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040031Z-td author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260925T040407Z-ge author=agent kind=event model=glm-5.3 pane=switchboard turn=307a41207fc72d9379aca6f6158aa155/db02c69b4db94ed6a794aa80d6eadbb2 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T040421Z-9f author=agent kind=event model=glm-5.3 pane=switchboard turn=307a41207fc72d9379aca6f6158aa155/db02c69b4db94ed6a794aa80d6eadbb2 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040425Z-88 author=agent kind=comment mode=plan model=glm-5.3 turn=307a41207fc72d9379aca6f6158aa155/db02c69b4db94ed6a794aa80d6eadbb2 -->
I'll plan this card. First, reading the card, its related cards, and the test code it names.

This is a single linear ask (plan one card), so no todo list. Continuing the reading: the case file with the `pgrep` use, the console-mode harness it sits in, and how a lint would be wired.

The case file matches the card's evidence. Now the harness helpers it uses — `h2kqRun`, `xcxdPump`, `XcxdShellContext` — and whether the harness already knows the pids it starts.



Now the Pane side — whether it already exposes the shell pid/pgid (`foregroundProcessGroup()` from #234Z) — and the console runner's registration and teardown.

`Pane` already exposes `shellPid()` and `foregroundProcessId()` in the runner's first case. Let me confirm what else Pane.h offers for group/pgid (the #234Z `tpgid` work), and where the pty child is spawned.

Odd — the runner calls `console.shellPid()`, but that search of `Pane.h` found nothing (likely the file's size). Trying a narrower pattern, and finding the pty spawn:

`Host` already exposes `shellPid()` and `foregroundProcessId()`. Now the pty spawn/kill side in `PaneRuntime.cpp`:

The shell runs in a systemd transient scope. Reading the start/stop region of `PaneRuntime.cpp` and the pid-tracking region:

`read_file` refuses files over 128 KiB, so I'll pull the regions with targeted searches instead:



The #234Z card confirms `foregroundProcessGroup()` (the `tpgid` helper) landed with it. Two more reads to ground the plan: the exact probe context in `234z_cases.h`, and the related card #8ABD. Plus how the python tests are wired, for the lint's home:

I have the full picture now. The card's hash was cleared from my earlier read, so re-reading #DSKT once for the write:

Writing `## Done means` first:

Now the plan itself:

**Done:** #DSKT now has its `## Done means` and a `## Plan` — the 234z probe switches to the pid/pgid the pane already reports (`Pane::foregroundProcessId()`, #234Z's `foregroundProcessGroup()`), the console-mode runner gains an RAII group-kill guard and an at-exit orphan scan, and a new pytest lint bans `pgrep`/`pkill`/`killall` in `tests/` with a self-checking allow-list.

Nothing needs your decision: the only coordination point is with #8ABD, which reworks waits in the same two files — the plan tells the runner to claim paths with `scripts/land.py begin` and keep its wait loop in the file's existing shape so 8ABD's `waitUntil` can absorb it. Ready for **Run**.

<!-- relay:entry 20260925T040425Z-bw author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T215446Z-aw author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session d1d07d15

<!-- relay:entry 20260925T215446Z-fd author=owner kind=progress pane=switchboard pane_token=d1d07d15-f7c4-4c19-a04f-a4f3230765dd -->
Claimed (d1d07d15) · working on it from a terminal pane

<!-- relay:entry 20260925T220837Z-k1 author=agent kind=event mention=8ABD model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
mentioned in #8ABD · 2026-09-25 · agent

<!-- relay:entry 20260925T220837Z-x9 author=agent kind=progress model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
Started. Claimed tests/234z_cases.h, consolemode_test.cpp, h2kq_cases.h, xcxd_ui_cases.h and the new lint with `land.py begin dskt`. #8ABD's session (b913fef9) is mid-rewrite of the same files: it has reworked every wait loop onto tests/pane_waits.h and restructured main() around `runRepeated`. Adapting: the orphan scan registers with atexit instead of editing main()'s returns, guards attach at the Pane construction lines (untouched by their diff), and the 234z case-2 probe swap is held until #8ABD lands (noted on #8ABD's thread). Harness block and guards going in now.

<!-- relay:entry 20260925T222748Z-1y author=agent kind=progress model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
Commit 1 landed: d25ab676 — the harness (pid registry, ProcessGuard killing foreground+shell process groups at case end, atexit scan that fails the run if a tracked pid survives) plus the review-pane guard, in tests/consolemode_test.cpp and tests/xcxd_review_cases.h. Verified: --234z-only passes beside a live decoy `sleep 30` loop; no orphans after runs; the scan's FAIL path proven with a throwaway build tracking an undying pid (prints FAIL orphaned pid … and exits 1). The pre-existing consolemode failures (ctrlClick group) reproduce identically on clean tip 134aa033 in a scratch build — signal ctest:consolemode (#VZ8C) territory, not this change. Still held for commit 2 behind #8ABD's landing: the 234z case-2 probe swap (already written on top of their restyle in the working tree), the remaining guard lines, and the lint (tests/test_no_name_matched_processes.py, passing). Evidence dir started: docs/qa_evidence/2026-09-25-dskt-no-name-matched-processes/.

<!-- relay:entry 20260925T222756Z-k2 author=agent kind=event model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off…

<!-- relay:entry 20260925T223133Z-dm author=agent kind=event mention=DJ3X model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
mentioned in #DJ3X · 2026-09-25 · agent

<!-- relay:entry 20260925T223952Z-0s author=agent kind=progress model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
Hand-off note (turn ending; commit 2 staged behind #8ABD). Everything for commit 2 is written and verified in the working tree, held by land session `dskt`: the 234z case-2 probe swap (group id from `Pane::foregroundProcessGroup()`, liveness `kill(-pgid, 0) != 0`, marker gone — written on top of #8ABD's uncommitted wait restyle, so it can only land after theirs), the guard lines in tests/h2kq_cases.h / tests/234z_cases.h / tests/xcxd_ui_cases.h, and the new tests/test_no_name_matched_processes.py lint (passes; catches violations; allow-list self-checks). Commit 1 (d25ab676: harness + atexit orphan scan) is on main. When #8ABD lands: `python3 scripts/land.py begin dskt --base main <the three case headers>` to refresh snapshots, rerun the decoy verification (`--234z-only` beside `until pgrep -f '[x]yzzy-no-such-marker-dskt'; do sleep 30; done`), then land commit 2 and move this card to needs-verification with docs/qa_evidence/2026-09-25-dskt-no-name-matched-processes/. Trigger to resume: #8ABD's landing note below, or the owner.

<!-- relay:entry 20260925T225458Z-r4 author=agent kind=note model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
#8ABD landed (3346744 + 6bb47dd): the 234z restyle is on main, so your kill(-pgid, 0) probe swap can go now. Note my landed case-2 keeps the `# 234z-two-press` marker inside the waitUntil pgrep predicate — replacing it per your plan is a clean single-hunk edit. Separate heads-up: main's relay-consolemode-tests is currently link-broken from #265N's landed call to cases::remoteShutdownIsGraceful() whose definition is still uncommitted (noted on #265N).

<!-- relay:entry 20260925T234225Z-53 author=owner kind=event pane=d1d07d15 -->
Released (d1d07d15) · the pane closed
