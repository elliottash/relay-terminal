# Pane-specific processes so one runaway pane cannot take down Relay

- **Status**: open
- **Component**: gui, worker, shell-integration
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: in one pane, a command that allocates memory past the pane limit (for example `python3 -c "b=bytearray(1<<40)"` or `stress-ng --vm 1 --vm-bytes 90%`) is killed; other panes, their shells and agents, and the Relay window keep running; `systemd-cgls --user` shows one scope per pane
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "make processes pane-specific, so if something blows up with memory, it doesnt crash the full terminal"

## Context

Shells (KonsolePart `startProgram`) and Python agent workers (`QProcess`) are already separate
processes, but all of them sit in Relay's single app cgroup. The kernel OOM killer picks the largest
`oom_score`, and systemd-oomd (default on Ubuntu/Fedora) kills a whole cgroup under memory pressure,
so a runaway `make -j` in one pane can take the Relay GUI and every pane with it. The GUI process
itself holds every pane's screen and scrollback; Relay's profile uses `HistoryMode=2` (unlimited,
file-backed in the temp dir, which is RAM when `/tmp` is tmpfs).

## Recommended direction

See `docs/NEXT-STEPS-RESEARCH.md` section B:

1. Launch each pane's shell and agent worker in its own transient user scope
   (`systemd-run --user --scope --unit=relay-pane-<id> -p MemoryMax=… -p MemoryHigh=…`), with a
   fallback to plain spawn when no user systemd is present.
2. Raise `oom_score_adj` of pane children (allowed unprivileged) so the kernel prefers them to the GUI.
3. Cap scrollback per pane (large fixed line count instead of unlimited) and keep the history file off tmpfs.
4. Show a pane banner when a pane's process was OOM-killed, with restart.

Out-of-process pane UI (browser-style) is not practical on Wayland; revisit only with the owned
terminal engine (`issues/features/2026-09-17-portable-terminal-engine.md`).
