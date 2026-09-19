---
id: 72NR
type: work
status: dropped
labels: [bug]
rank: zzy
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: [], related: [], github: null, merged_into: JH5M}
---
# Per-pane 80 ms shell poll re-parses state.json and probes /proc every tick

## Request
look for cleanup opportunities

## Findings
From the 2026-09-17 cleanup audit (verified by reading the code):

`Pane::pollShell` (src/main.cpp:7101) runs on an 80 ms timer per pane and, on every tick:

1. opens, reads and `QJsonDocument::fromJson`-parses `state.json` *before* the sequence-number check that discards unchanged files (~7113);
2. via `refreshShellReady()` → `readlineReady()` (src/main.cpp:7059) opens `/proc/<pid>/fd/0` (`tcgetattr`) and reads `/proc/<pid>/stat` in `foregroundGroup()` (src/main.cpp:7073), even when nothing changed.

With N panes that is 12.5 N file opens + JSON parses/sec plus /proc syscall churn on the GUI thread.

**Suggested fix:** stat the file (or `QFileSystemWatcher`) and only read/parse when mtime/size changes; skip `readlineReady()` when `!m_promptReported && !m_loading`; consider dropping to the 250 ms `m_programPoll` rate once the shell is known idle. Note the deliberate comment at 7102 ("Recheck on every tick") — the fix must preserve prompt-detection latency.

Related low-priority item: `shellHistory()` (src/main.cpp:6202) re-parses up to 512 KiB of `.bash_history` on any mtime change; a tail-only append would avoid full reparses.

## Resolution
Merged into [#JH5M](../needs_qa_llm/2026-09-18-idle-panes-cost-less.md) on 2026-09-19: Same fault, same landed fix: #JH5M removed exactly what #72NR's audit found (state.json re-parsed and /proc probed on every 80 ms tick) and carries the commits and QA checklist; 72NR had none.

Nothing was thrown away: the text above is also kept on #JH5M under `## Merged in`, and this card stays here so `#72NR` keeps resolving.
