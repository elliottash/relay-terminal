---
id: PF4K
type: work
status: needs-verification
labels: [feature, performance]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20'
links: {plans: [], commits: [ccb31a8e, d2923c9b, 34b5175e], evidence: [docs/qa_evidence/2026-09-20-perf-profile/REPORT.md], related: [9MYY, 7BM4, 7M6E, TZWF, 057J, 6W0Z, MDSG, PPR4, N5JJ], github: null}
---
# Profile Relay on spark and sphinxpad: performance issues and improvements

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Plan
**Goal.** Measured findings, not a code read: where Relay spends CPU, wall time, memory and wakeups,
on spark (aarch64, Qt 5.15) and on sphinxpad (x86_64 i7-1365U, Ubuntu 26.04), each with a proposed
improvement and its expected gain.

**Findings so far.** `perf_event_paranoid=4` on both machines, but passwordless `sudo perf` works on
both, so the app target is profilable without touching a sysctl. `build/` is RelWithDebInfo and holds
other sessions' uncommitted code, so every measurement is of a clean export of `main`.

**Steps.**
1. Clean export of `main` built RelWithDebInfo on spark (scratch) and on sphinxpad (`~/relay-perf`).
2. One Opus subagent per area, measuring only, evidence under
   `docs/qa_evidence/2026-09-20-perf-profile/<area>/`: terminal engine and paint path; startup, idle
   wakeups and memory; agent transcript streaming and long conversations; the Python worker;
   the Switchboard pane and `relay-board.py`.
3. The same scenarios on sphinxpad, side by side.
4. Synthesis in `docs/qa_evidence/2026-09-20-perf-profile/REPORT.md`; one card per fault worth fixing.

**Risks.** Other sessions load this machine, so numbers carry noise: three runs each, load average
recorded. Profilers measure; they change no product code.

**Verify.** Every finding names the command that reproduces its number.

## Result
Five Opus profilers measured a clean export of `main` (ccb31a8e) on spark and on sphinxpad (Qt5 and
Qt6). The synthesis is `docs/qa_evidence/2026-09-20-perf-profile/REPORT.md`; each area's
`FINDINGS.md` beside it holds the commands behind every number. No product code was changed, except
that building on sphinxpad found `tests/wordwrap_test.cpp` did not compile on Qt6 (fixed, d2923c9b).

Cards for the faults worth fixing: #7M6E (the Switchboard stops loading above ~1,160 cards),
#TZWF (the worker reloads the CA store per request; bytecode never cached when installed),
#057J (idle wakeups; `QSettings` on hot paths), #6W0Z (terminal paint path), #MDSG (Sessions search
and large files), #PPR4 (tool output sent and discarded; per-turn cost grows), #N5JJ (the board
watcher misses appends — correctness). #9MYY's three claims were measured; the verdict is on its thread.

For the owner, in REPORT.md under "For the owner to decide": one worker per pane (~27 MB each), the
25–41 KB system prompt, whole-file session saves, Qt6 for the 26.04 build, and the blocking
`systemd-run` probe before the first window.

## QA checklist
- [ ] REPORT.md's table matches the numbers in the five `FINDINGS.md` files <!-- t:q1 -->
- [ ] Re-run one number per area from its harness and get the same order of magnitude, e.g. `python3 -c "import time,urllib.request as u; t=time.process_time(); [u.build_opener() for _ in range(20)]; print((time.process_time()-t)/20*1000)"` ≈ 12 ms (#TZWF) <!-- t:q2 -->
- [ ] A synthetic 1,200-card board shows "Loading the Switchboard…" forever (`board/harness/`), as #7M6E says <!-- t:q3 -->
- [ ] Each of the seven cards names the file and line of its cause and they exist on `main` <!-- t:q4 -->
