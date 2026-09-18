---
id: F9SD
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker, shell-integration]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-17
rank: nt
created: '2026-09-17'
acceptance: in one pane, a command that allocates memory past the pane limit (for example `python3 -c "b=bytearray(1<<40)"` or `stress-ng --vm 1 --vm-bytes 90%`) is killed; other panes, their shells and agents, and the Relay window keep running; `systemd-cgls --user` shows one scope per pane
source: '`issues/feature_intake.txt`, "make processes pane-specific, so if something blows up with memory, it doesnt crash the full terminal"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Pane-specific processes so one runaway pane cannot take down Relay

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

## Behavior as implemented (2026-09-17)

- Shell: `systemd-run --user --scope --quiet --unit=relay-pane-<token8>-shell-<n> -p MemoryMax=8G
  -p MemoryHigh=6G -p MemorySwapMax=2G -p KillSignal=SIGHUP -p TimeoutStopSec=5 -p OOMPolicy=continue -- /bin/bash ...`
  passed to KonsolePart `startProgram`. `--scope` execs in place: the PID Relay tracks is bash's.
- Worker: same wrapper around `python3 -S -u worker.py` with `MemoryMax=2G`, `MemorySwapMax=512M`,
  `OOMPolicy=stop`; the stdin/stdout protocol is unchanged.
- Settings (`[isolation]` in relay.conf): `enabled`, `shell_memory_max`, `shell_memory_high`,
  `shell_swap_max`, `shell_oom_policy` (`continue` default, `stop` ends the whole shell),
  `agent_memory_max`, `agent_swap_max`. Invalid sizes fall back to defaults.
- Probe: `systemd-run --user --scope --quiet -- true`, once per process; if it fails panes start
  unisolated and the status bar says so once.
- OOM priority: bash integration writes 300 to `/proc/$$/oom_score_adj`; the worker raises its own to 500
  (never lowers). The GUI keeps the default.
- Detection: a 1 s poll reads the shell scope's `memory.events` `oom_kill`; an increase shows
  "A command in this pane was stopped because it ran out of memory (limit X). The shell is still running."
  A dead shell PID (Konsole keeps a "Program crashed" view instead of closing) or a failed scope with
  `Result=oom-kill` shows "This pane's shell was stopped because it ran out of memory (limit X)" with
  Restart shell (Ctrl+Shift+R), which replaces the terminal part in the same cwd. A stopped worker
  shows "The agent worker stopped/was stopped" with Restart agent. Normal `exit` still closes the pane.
  Failed scopes are `reset-failed` after their result is read.
- Scrollback capped at 20,000 lines (`HistoryMode=1`).

## Implementer check (not a QA verdict)

2026-09-17 under Xvfb, isolated XDG_CONFIG_HOME:
- Two panes → `systemctl --user list-units 'relay-pane-*'` listed `-shell-1` and `-agent-1` scopes
  for each; inside a pane `echo $$`, `/proc/$$/cgroup` and `oom_score_adj` printed bash's PID
  (matching the PID seen from outside), `relay-pane-…-shell-1.scope`, and `300`.
- `shell_memory_max=200M, shell_memory_high=infinity, shell_swap_max=0`: a python allocation loop
  printed `Killed`, the banner appeared, the shell kept working, the other pane ran `echo other-pane-ok`.
- Same with `shell_oom_policy=stop`: the shell stopped, the restart banner appeared, Ctrl+Shift+R started
  a working shell in `relay-pane-…-shell-2.scope`; no failed scopes left behind.
- `systemctl --user kill --signal=SIGKILL` on the agent scope → "The agent worker was stopped" banner;
  Ctrl+Shift+R → `-agent-2` scope, "Agent ready · kimi-k3", natural language routed to AGENT.
- Password detection (`read -s`) and composer routing worked in scoped shells.
- `enabled=false`: shell ran in Relay's own app scope; no `relay-pane-*` units.
- Found while testing: interactive bash ignores SIGTERM, so a stopped scope hung ~90 s → `KillSignal=SIGHUP`;
  `MemoryHigh` throttling plus swap delayed kills for minutes → swap caps added.
- Tests: `tests/test_isolation.py` (worker OOM score, never lowered, worker process score, integration line).

Evidence: `docs/qa_evidence/2026-09-17-pane-isolation/` (`implementer-command-oom-continue.png`,
`implementer-shell-oom-stop-restart.png`, `implementer-worker-stopped-restart.png`).

## QA checklist

1. On a desktop session, `systemd-cgls --user` shows one shell and one agent scope per pane.
2. With a 1G shell limit, `stress-ng --vm 1 --vm-bytes 2G` (or the python loop) in one pane: that
   command is stopped with the banner; other panes and the window keep running.
3. `shell_oom_policy=stop`: the shell stops, Restart shell works, cwd is preserved.
4. Kill a worker scope: Restart agent reconfigures the saved model.
5. Default limits do not interfere with ordinary builds (`make -j`, `cargo build`).
6. Non-systemd environment (e.g. a container): panes start, one status message, no errors.
7. Closing a pane or window leaves no `relay-pane-*` units behind.
