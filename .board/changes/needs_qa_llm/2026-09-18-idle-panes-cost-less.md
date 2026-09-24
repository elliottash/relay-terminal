---
id: JH5M
type: work
status: needs-qa-llm
labels: [change, bug, performance]
component: [gui, engine]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 and a Claude Opus 5 subagent (Claude Code, milestone review), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: Idle CPU with eight panes is lower than before in both layouts, and nothing about shell readiness, staged commands or password prompts behaves differently; `ctest` passes
source: 'milestone review of the code base, 2026-09-18: idle CPU measured at 0.25 % of a core per pane, on screen or not'
links: {plans: [], commits: [32873ac], evidence: [], related: [J314], github: null, merged_from: [72NR]}
---
# Idle panes cost less: a quiet background tab polls slower, and a tick no longer opens /proc

## Report

Every pane polls its shell every 80 ms. Traced under `strace`, one tick was an open / `TCGETS` /
close of `/proc/<shell>/fd/0`, a read and parse of `/proc/<shell>/stat`, two `TIOCGPGRP` and an
open / read / JSON-parse of `state.json` — about 0.25 % of a core per pane, idle, for ever, and the
same for a pane in a background tab as for the one being typed in.

## Change

1. **32873ac** — a pane that is off screen with nothing in flight polls every 400 ms
   (`Pane::tunePoll`), and is back at 80 ms when shown, given a command, or holding a queue;
   `state.json` is `stat()`ed and only read when it changed.
2. **This commit** — the engine owns the pty master, and on Linux `tcgetattr` and `TIOCGPGRP` on
   the master answer for the slave, with no controlling-terminal restriction (proved with a
   program run under `setsid`; `tests/test_shell.py` already relied on it). New
   `Pty::termiosFlags()` → `TerminalSession` → `TerminalBackend::termiosFlags()`, capability
   `LineDiscipline`. `Pane::readlineReady()` and `Pane::terminalMode()` ask it first and keep their
   `/proc` code as the fallback for a backend that cannot answer. The rules are unchanged: ready =
   raw mode **and** the shell's group is the tty's foreground group; a password prompt =
   canonical **and** no echo.

| idle, 20 s windows under Xvfb | at the start | after 1 | after 2 |
|---|---|---|---|
| eight tabs | 1.80 % | 0.55 % | 0.40 % |
| eight split panes, all visible | 1.95 % | 1.60–1.80 % | 1.25–1.30 % |

GUI-thread system calls in a 10 s idle window, eight visible panes: 13,091 → 6,300. Every
`/proc/<pid>/fd/0` and `/proc/<pid>/stat` open is gone.

## QA checklist

1. **Staged commands.** Type `echo hi`, Enter: it runs. Ten in a row, quickly: all run, in order.
2. **A full-screen program is not "ready".** Run `less README.md`, type a command in the prompt
   box: it is queued ("runs when the terminal is free"), never typed into `less`; quit `less` and
   it runs.
3. **Password prompts.** `sudo -k true` (or `python3 -c 'import getpass; getpass.getpass()'`):
   the masked field appears, takes the line, and the normal prompt box comes back.
4. **`read -s` and `stty -echo`** are treated as password prompts; `stty -icanon` alone is not.
5. **Background tab.** A long command in tab 1, work in tab 2: tab 1 still reports "Command
   finished" to the bell, and a command typed the instant you switch back runs.
6. **Measure.** `top -p $(pgrep -x relay)` with eight idle tabs sits near 0.4 %, not 1.8 %.

## Merged in
### #72NR — Per-pane 80 ms shell poll re-parses state.json and probes /proc every tick (2026-09-19)

Merged from `issues/changes/2026-09-17-per-pane-80-ms-shell-poll-re-parses-state-json-a.md` (ready): Same fault, same landed fix: #JH5M removed exactly what #72NR's audit found (state.json re-parsed and /proc probed on every 80 ms tick) and carries the commits and QA checklist; 72NR had none.

#### Request
look for cleanup opportunities

#### Findings
From the 2026-09-17 cleanup audit (verified by reading the code):

`Pane::pollShell` (src/main.cpp:7101) runs on an 80 ms timer per pane and, on every tick:

1. opens, reads and `QJsonDocument::fromJson`-parses `state.json` *before* the sequence-number check that discards unchanged files (~7113);
2. via `refreshShellReady()` → `readlineReady()` (src/main.cpp:7059) opens `/proc/<pid>/fd/0` (`tcgetattr`) and reads `/proc/<pid>/stat` in `foregroundGroup()` (src/main.cpp:7073), even when nothing changed.

With N panes that is 12.5 N file opens + JSON parses/sec plus /proc syscall churn on the GUI thread.

**Suggested fix:** stat the file (or `QFileSystemWatcher`) and only read/parse when mtime/size changes; skip `readlineReady()` when `!m_promptReported && !m_loading`; consider dropping to the 250 ms `m_programPoll` rate once the shell is known idle. Note the deliberate comment at 7102 ("Recheck on every tick") — the fix must preserve prompt-detection latency.

Related low-priority item: `shellHistory()` (src/main.cpp:6202) re-parses up to 512 KiB of `.bash_history` on any mtime change; a tail-only append would avoid full reparses.
