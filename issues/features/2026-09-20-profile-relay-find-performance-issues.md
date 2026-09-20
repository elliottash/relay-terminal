---
id: PF4K
type: work
status: executing
labels: [feature, performance]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [9MYY, 7BM4], github: null}
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
