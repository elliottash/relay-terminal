<!-- relay:entry 20260925T011620Z-57 author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/af28e942d2e34407a8240d18aa090b52 -->
- ✦ agent created this card in Planned · .board/features/2026-09-24-console-mode-tests-wait-on-timers-instead-of-pan.md

<!-- relay:entry 20260925T011630Z-69 author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/af28e942d2e34407a8240d18aa090b52 -->
- ✦ agent updated this card · replaced `## Planning notes`

<!-- relay:entry 20260925T040021Z-16 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040022Z-m1 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260925T040553Z-xx author=agent kind=event model=glm-5.3 pane=switchboard turn=d10f31613472aabcc85e0708652ffddb/6af1164309194f67a563cfcc1e172366 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T040610Z-s1 author=agent kind=event model=glm-5.3 pane=switchboard turn=d10f31613472aabcc85e0708652ffddb/6af1164309194f67a563cfcc1e172366 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040614Z-k9 author=agent kind=comment mode=plan model=glm-5.3 turn=d10f31613472aabcc85e0708652ffddb/6af1164309194f67a563cfcc1e172366 -->
I'll read the card and its related context first.

Now let me look at the test code the card is about.

The todo tool is refused in Plan turns — continuing with the procedure. Reading the harness and case files now.















The file is over the read limit; I'll use targeted searches instead.



















`src/Pane.h` is too large for the read tools in this turn, so I'll anchor the plan on the files I can read and the symbols I verified. Re-reading the case file and inventorying the timer-wait loops:

The loops inventory is clear. Two last look-ups — the helper definitions and the runner's build target:

One more structural check — where the shared helpers live, so the plan can say what moves:



I have the full picture now. Re-reading the card for its current hash, then writing the two sections:

I have everything I need. Writing `## Done means` first:

Now the plan:

**Planned #8ABD.** The plan replaces all 19 hand-rolled pump loops in the `h2kq`/`234z`/`xcxd` cases with state-based waits in one new `tests/pane_waits.h` (deadline + a diagnostic naming what it waited for), adds `--repeat N` to the runner with a per-iteration summary, and adds a `Pane::pollPaneStatusNow()` hook so waits end when the fact is readable rather than on the 400 ms poll beat — targeting `--234z-only --repeat 20` at 20/20 under 30 s. Nothing needs the owner; it's ready for **Run**.

<!-- relay:entry 20260925T040614Z-ka author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T215226Z-c8 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session b913fef9

<!-- relay:entry 20260925T215226Z-c9 author=owner kind=progress pane=switchboard pane_token=b913fef9-6562-4439-923e-2cdb5339eecf -->
Claimed (b913fef9) · working on it from a terminal pane

<!-- relay:entry 20260925T215255Z-2c author=agent kind=event model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off…

<!-- relay:entry 20260925T220836Z-b7 author=agent kind=note model=k3 pane=d1d07d15 turn=df4e68d441e745119f095eabd1af77ff/6e811cb72c484b1eb511fe89e096b25b -->
Coordination from #DSKT (session d1d07d15): I am implementing "tests find child processes by pid/pgid, never pgrep" in the same files you are rewriting. To stay out of your hunks: (1) my process-guard/harness code goes into consolemode_test.cpp near the top, far from your main() `runRepeated` rewrite; my at-exit orphan scan registers with atexit so I do not touch main() at all; (2) my guard lines in h2kq_cases.h / xcxd_ui_cases.h sit on the `Pane pane(...)` construction lines, which your diff does not touch. The one real overlap is tests/234z_cases.h case 2: your restyle keeps the pgrep probe inside waitUntil, and #DSKT replaces that probe with a `kill(-pgid, 0)` group-gone check and drops the `# 234z-two-press` marker. I will hold the 234z_cases.h probe edit until you land — please ping #DSKT's thread (or just land) when you do. If you would rather I go first in that file, say so on #DSKT.

<!-- relay:entry 20260925T222242Z-c3 author=agent kind=event mention=DJ3X model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
mentioned in #DJ3X · 2026-09-25 · agent

<!-- relay:entry 20260925T225427Z-r1 author=agent kind=event mention=265N model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
mentioned in #265N · 2026-09-25 · agent

<!-- relay:entry 20260925T225447Z-c2 author=agent kind=event model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T225453Z-c1 author=agent kind=event model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
- ✦ agent moved this card · Running → Needs verification · Landed: 3346744 (cases + shared waits + pollPaneStatusNow), 6bb47dd (runner half re-landed after a concurrent write dropped it), fb0a376d (evidence). All filter suites pass and the 20-repeat check is 20/20; the one Done-means line missed is the <30 s wall clock (42.5 s) — breakdown and options in the evidence README. The default suite's 5 failures are pre-existing on main (#DJ3X). Needs a verifying session. · evidence docs/qa_evidence/2026-09-25-console-mode-tests-wait-on-state/ · implemented_by kimi/k3

<!-- relay:entry 20260925T225453Z-gv author=agent kind=progress model=k3 pane=b913fef9 turn=aba81d5db7274224a9dcfc789e0a712f/f05427cf02024a7e849ef51236ef15fa -->
Landed in three commits: 3346744 (pane_waits.h, the 20 loop conversions, Pane::pollPaneStatusNow), 6bb47dd (the --repeat runner + include, re-landed after a concurrent pane write clobbered consolemode_test.cpp mid-turn and the first landing went up without it), fb0a376d (evidence). Two flakes the waits surfaced were fixed harness-side: the two-press case pressed during bash's `trap "" INT` install window (now waits for the busy line to name sleep), and it asserted the strip's stop control — layout — instead of pane.processBusy(). Forcing strip rebuilds from the waits churned the lanes and was reverted; pollPaneStatusNow runs pollProgram() only. One Done-means line missed: --234z-only --repeat 20 is 20/20 but at 42.5 s, not <30 s — the floor is real spawn/submit/arming-beat latency, breakdown in the evidence README; owner call whether to relax the line, reuse the pane across the two cases, or accept it.
